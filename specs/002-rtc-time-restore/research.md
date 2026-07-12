# Research: RTC Time Restore After Reboot

**Feature**: 002-rtc-time-restore | **Date**: 2026-07-12

## R1 — Time backup mechanism: internal RTC domain vs. external RTC chip

**Decision**: Use the ESP32's built-in RTC domain, kept powered through main-power interruptions by the T-Display module's onboard rechargeable battery. No external clock chip.

**Rationale**: Directed by the spec (Assumptions). It requires zero additional hardware on the ESP32 side, zero new GPIOs, and no bus driver — the T-Display already has a battery connector and charging circuit, so the only physical prerequisite is plugging in a Li-Po cell. ESP-IDF's timekeeping already survives every reset type except RTC-domain power loss, so the firmware work reduces to *knowing whether the surviving time is trustworthy*.

**Alternatives considered**:
- **External DS3231 module (I2C)**: ±2 ppm accuracy and multi-year CR2032 retention, but adds a hardware module, wiring (GPIO21/22), an I2C driver, and BCD codec/atomicity concerns. Rejected: the spec explicitly selects the internal RTC; accuracy and retention of the internal approach are sufficient for the success criteria (see R6).
- **Periodic persistence to NVS/littlefs**: survives total power loss without any battery, but can only restore a *stale* time (up to the persistence interval old, unbounded if the outage is long), which violates "never show a wrong time as if correct", and adds flash wear. Rejected.

## R2 — How ESP-IDF system time persists across resets

**Decision**: Rely on ESP-IDF's default system-time source ("RTC and high-resolution timer"): `settimeofday()` stores the boot-time offset in RTC slow memory, and after any soft reset (esp_restart, panic, watchdog, brownout recovery, OTA restart) `gettimeofday()` returns correct wall time without any action from application code. Pin this explicitly in `sdkconfig.defaults` (`CONFIG_ESP_SYSTEM_TIME_SOURCE_RTC_AND_FRC=y` / the ESP_TIME_FUNCS_USE_RTC_TIMER choice) so a stray config change can't silently break the feature.

**Rationale**: This is built-in newlib/esp_system behavior; re-implementing it would duplicate the IDF. The persistence boundary is exactly the RTC power domain: time survives while the RTC domain is powered (including through main-power loss when the battery carries the board) and is lost on a true cold power-up — which is precisely the boundary the spec's Story 3 handles.

**Alternatives considered**: High-resolution-timer-only time source (loses time on every reset — defeats the feature); manual save/restore of `struct timeval` in application code (redundant with IDF behavior).

## R3 — Detecting whether the surviving time is valid

**Decision**: A validity record in RTC slow memory, declared `RTC_NOINIT_ATTR`: `{ magic, version, last_sync_utc, crc32 }`. It is written (with CRC over the payload) every time SNTP synchronization succeeds, and checked once at boot. Boot logic: record magic/CRC valid **and** current system time within plausibility bounds (R4) → restore accepted (`time_source = rtc`); otherwise `time_source = none`.

**Rationale**: The record lives in the same power domain as the clock itself, so the two fail together: if the RTC domain lost power (no/dead battery), the record is garbage (CRC fails) *and* the time is gone — one check covers both. After a cold power-up RTC noinit memory contains random bits; a 32-bit magic plus CRC32 makes a false "valid" astronomically unlikely. Storing `last_sync_utc` also gives the management page a useful "last synchronized at" value for free.

**Alternatives considered**:
- **Reset-reason inspection** (`esp_reset_reason() == ESP_RST_POWERON` ⇒ invalid): distinguishes cold boot from soft reset, but cannot tell whether time was *ever* NTP-synced before the reset (e.g., reboot after a fresh flash where time was never set). Used only as supplementary logging, not as the validity criterion.
- **Flag in NVS**: survives power loss while the time itself does not — the flag and the clock could disagree, producing exactly the bogus-restore the spec forbids. Rejected.

## R4 — Plausibility bounds for the restored time

**Decision**: Accept the surviving system time only if `BUILD_EPOCH ≤ now ≤ BUILD_EPOCH + 20 years`. `BUILD_EPOCH` is the firmware build time as a Unix timestamp, injected at compile time by the `rtc_time` component's CMakeLists via `string(TIMESTAMP ... UTC)` as a compile definition.

**Rationale**: FR-004 mandates at minimum rejecting times before the firmware build date. CMake injection is deterministic and avoids parsing `__DATE__` strings at runtime. The lower bound is safe in all cases — if the surviving clock is *correct*, "now" necessarily postdates the build of whatever firmware is running (including after an OTA), so a valid time can never be rejected. The 20-year upper bound rejects corrupted-but-CRC-lucky values and absurd futures while never triggering in the device's realistic lifetime.

**Alternatives considered**: Fixed hardcoded epoch (rots, needs manual bumps); no upper bound (leaves a corrupted-future-time hole).

## R5 — What actually happens on the T-Display during a "power interruption"

**Decision**: Document and design for the module's real behavior: when main (USB) power drops with a battery attached, the **whole board keeps running from the battery** — the clock never stops, the display stays on, and the "restore" path isn't even needed until the battery itself is exhausted. The µA-level RTC-domain-only retention from the spec's assumption is the *limit* case (deep sleep is not used in this firmware); the delivered guarantee is: time survives main-power interruptions while the battery has charge, and survives all software resets/reboots regardless of battery. If the battery is absent or fully depleted at power-off, the RTC domain loses state and Story 3 degradation applies.

**Rationale**: Honest mapping of the spec's assumption onto the actual hardware (battery feeds the board through the charger IC, not a dedicated RTC rail). The user-visible outcomes promised by the spec (correct time after reboot without network; graceful "not available" when the battery is dead) are all met. Adding USB-loss detection + deep-sleep timekeeping would extend battery endurance from hours to months but is new scope (display off, wake logic) — out of scope for this feature and not required by any SC.

**Alternatives considered**: Auto deep-sleep on main-power loss (better endurance, but changes product behavior and adds scope); rejecting the battery approach in favor of DS3231 (contradicts the spec).

## R6 — Accuracy budget vs. SC-002 (≤1 min drift over 7 offline days)

**Decision**: No extra discipline mechanism needed; the default clock sources meet the budget.

**Rationale**: While running (mains or battery), system time advances on the 40 MHz main crystal, worst case ±40 ppm ≈ 3.5 s/day ≈ 24 s over 7 days — inside the 60 s budget with 2.5× margin. The less accurate 150 kHz internal RC slow clock matters only across reset gaps (seconds long) and during deep sleep (not used) — negligible contribution. When online, hourly SNTP polls (feature 001 behavior) keep drift near zero and refresh the validity record.

**Alternatives considered**: Periodic re-discipline against an external reference (none exists offline — vacuous); external 32.768 kHz crystal (pads not populated on T-Display; irrelevant while esp_timer runs).

## R7 — Integration points in the existing codebase

**Decision**: Touch exactly four existing files, plus `main.c` and `sdkconfig.defaults`:

| Location | Change |
|---|---|
| `components/app_ctx/app_ctx.h` | Add `app_time_source_t {NONE, RTC, NTP}`, `app_state.time_source`, event `APP_EVT_TIME_RESTORED`. Keep `time_synced` (true only for NTP) for backward compatibility inside 001 code. |
| `components/wifi_mgr/wifi_mgr.c` (`sntp_sync_cb`) | After the existing `time_synced = true` / `APP_EVT_TIME_SYNCED`, call `rtc_time_mark_synced()` and set `time_source = NTP`. |
| `components/display/display.c` | Gate the time area on `time_source != NONE` instead of `time_synced`; subscribe to `APP_EVT_TIME_RESTORED` in addition to `APP_EVT_TIME_SYNCED`. |
| `components/web_server/handlers_mgmt.c` (`/api/status`) | Add `"time_source": "none"\|"rtc"\|"ntp"` and `"time_last_sync"` (Unix seconds or null). |
| `main/main.c` | Call `rtc_time_restore()` after NVS init, before task creation, so the first display frame can show restored time. |
| `sdkconfig.defaults` | Pin the system-time source to RTC + high-resolution timer. |

**Rationale**: Follows the 001 component/event conventions already in the code (verified by inspection of `wifi_mgr.c:100-107`, `app_ctx.h:46-57`, `display.c:146-173`, `handlers_mgmt.c:79-149`); no new tasks; the display keeps its existing "time unavailable" rendering — only the gating condition widens.

**Alternatives considered**: A dedicated event consumer component for sync events (deeper event graph for no benefit); replacing `time_synced` everywhere (touches more 001 code than necessary — deferred until a natural refactor).
