# Data Model: RTC Time Restore After Reboot

**Feature**: 002-rtc-time-restore | **Date**: 2026-07-12

## 1. Validity Record (RTC slow memory, `RTC_NOINIT_ATTR`)

The battery-backed proof that the surviving system time originated from a real network synchronization. Lives in the same power domain as the clock, so both survive or both perish (research.md R3).

| Field | Type | Meaning |
|---|---|---|
| `magic` | `uint32_t` | Constant `0x52544301` ("RTC" + version 1). Random after RTC-domain power loss. |
| `version` | `uint32_t` | Record layout version, `1`. Allows future extension without misreading. |
| `last_sync_utc` | `int64_t` | Unix seconds (UTC) of the most recent successful SNTP synchronization. |
| `crc32` | `uint32_t` | CRC32 (`esp_rom_crc32_le`) over `magic`, `version`, `last_sync_utc`. |

**Write rule**: rewritten atomically-enough (single RAM struct update, CRC last) on every successful SNTP sync — after newlib has already applied the new time. Never written at any other moment, so a crash mid-write can at worst leave a CRC-invalid record, which reads as "no valid time" (fail-safe).

**Read rule** (once, at boot): record is *intact* iff `magic`, `version` match and CRC verifies.

## 2. Plausibility Bounds (compile-time constants)

| Constant | Value | Source |
|---|---|---|
| `RTC_TIME_MIN_VALID` | `BUILD_EPOCH` — firmware build time, Unix seconds UTC | Injected by `components/rtc_time/CMakeLists.txt` via `string(TIMESTAMP ... UTC)` |
| `RTC_TIME_MAX_VALID` | `BUILD_EPOCH + 20 years` (630 720 000 s) | Same |

## 3. Restore Decision (pure function, unit-testable)

Inputs: record intact? (bool), current system time `now` (Unix seconds).

```text
restore_valid(intact, now) :=
    intact
    AND now ≥ RTC_TIME_MIN_VALID     (FR-004: not before firmware build)
    AND now ≤ RTC_TIME_MAX_VALID     (corruption guard)
```

- `true`  → time source becomes `RTC`; system time is left as-is (ESP-IDF already restored it).
- `false` → time source stays `NONE`; system time is untouched but *not displayed*; behavior identical to today's "time not available" (FR-004, FR-006).

## 4. Time Source State (`app_ctx`)

```text
typedef enum {
    APP_TIME_SOURCE_NONE,   /* no trustworthy time; display shows "not available"  */
    APP_TIME_SOURCE_RTC,    /* restored from battery-backed clock, not yet verified */
    APP_TIME_SOURCE_NTP,    /* network-synchronized this boot                       */
} app_time_source_t;
```

**Transitions** (monotonic within a boot — trust only increases):

| From | Event | To | Side effects |
|---|---|---|---|
| — (boot) | `rtc_time_restore()` finds valid record + plausible time | `RTC` | post `APP_EVT_TIME_RESTORED` |
| — (boot) | restore check fails | `NONE` | none (existing behavior) |
| `NONE` or `RTC` | SNTP sync succeeds (`sntp_sync_cb`) | `NTP` | `time_synced = true`, write validity record, post `APP_EVT_TIME_SYNCED` |
| `NTP` | subsequent periodic SNTP syncs | `NTP` | refresh `last_sync_utc` in record |

No transition ever lowers the state during a boot; loss of network keeps `NTP` (time keeps counting internally — existing 001 behavior).

**Relationship to existing fields**: `app_state.time_synced` keeps meaning "NTP-synced this boot" (`time_source == NTP`); `time_source != NONE` is the new "time is displayable" predicate (FR-005).

## 5. Status Reporting (management API)

`GET /api/status` gains (see contracts/status-api.md):

| JSON field | Type | Value |
|---|---|---|
| `time_source` | string | `"none"` \| `"rtc"` \| `"ntp"` |
| `time_last_sync` | number \| null | `last_sync_utc` from an intact validity record, else `null` |

## 6. Explicitly Not Stored

- No new NVS keys (timezone, display mode etc. unchanged — FR-008 keeps stored time UTC).
- No flash-persisted timestamps (rejected in research.md R1 — stale-restore hazard).
- No battery level (not measurable for the RTC retention path; out of scope per spec assumptions).
