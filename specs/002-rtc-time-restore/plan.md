# Implementation Plan: RTC Time Restore After Reboot

**Branch**: `002-rtc-time-restore` | **Date**: 2026-07-12 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/002-rtc-time-restore/spec.md`

## Summary

Keep time across reboots and main-power interruptions using the ESP32's built-in RTC domain, powered through the T-Display module's onboard rechargeable battery — no external clock chip, no new wiring. ESP-IDF already carries system time across any reset that doesn't power down the RTC domain; what this feature adds is (1) a battery-backed **validity record** in RTC slow memory (magic + CRC + last-sync timestamp, written on every successful SNTP sync), (2) a boot-time **restore check** that validates the surviving system time (validity record intact + plausibility bounds against the firmware build date) and publishes a time-source state (`none` / `rtc` / `ntp`), and (3) surfacing that state to the display ("time unavailable" gating) and the management `/api/status` endpoint. A missing/depleted battery makes the validity check fail cleanly, degrading to the pre-existing NTP-only behavior.

## Technical Context

**Language/Version**: C (C17), ESP-IDF ≥ 6.0 (per `main/idf_component.yml`), CMake build via `idf.py`

**Primary Dependencies** (all ESP-IDF built-ins; no new managed components):
- ESP-IDF system time (newlib + RTC timer time source) — persists time across soft resets while the RTC domain is powered
- `RTC_NOINIT_ATTR` RTC slow memory — battery-backed validity record
- `esp_rom_crc32_le` — record integrity check
- Existing feature-001 stack: `esp_sntp` callback in `wifi_mgr`, `app_ctx` shared state/events, `web_server` status handler, `display` UI

**Storage**: No new NVS keys, no littlefs files. State lives in RTC slow memory (survives exactly as long as the clock itself — they share the RTC power domain, so validity and time can never disagree after a power event). Validity lower bound is the firmware build epoch injected at compile time via CMake.

**Testing**: Unity component tests for pure logic (validity-record CRC/magic check, plausibility bounds, time-source state transitions) with injected record/time values; `idf.py build` CI gate; hardware validation per quickstart.md (offline reboot, battery-removed cold boot, sync-refresh scenarios)

**Target Platform**: ESP32 (Xtensa dual-core, no PSRAM), Tenstar T-Display, 16 MB flash, with the module's battery connector populated (rechargeable Li-Po, charges automatically from main power)

**Project Type**: Embedded firmware — extends the existing single ESP-IDF project at repo root

**Performance Goals**: Restored time visible within the normal boot period (≤15 s from power-on, SC-001); restore check itself is O(µs) — a memory read + CRC over ~16 bytes; offline drift ≤1 min over 7 days (SC-002; main 40 MHz crystal ±40 ppm worst case ≈ 24 s/week)

**Constraints**: Restore must never block boot (pure in-memory check, no bus I/O, nothing to time out); validity record must be written only after time is actually set (ordering: `settimeofday` by SNTP first, record second); time kept timezone-independent (system time is UTC; TZ applied at display, per FR-008); no new FreeRTOS tasks

**Scale/Scope**: 1 new component (`rtc_time`), small touches in 4 existing components (`app_ctx`, `wifi_mgr`, `display`, `web_server`), 0 new GPIOs, 1 new status field

**Hardware additions**: None on the ESP32 side. Prerequisite: a rechargeable battery connected to the T-Display module's battery connector so the board (and with it the ESP32 RTC domain) stays powered through main-power interruptions. The 001 GPIO map is unchanged.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

`.specify/memory/constitution.md` is the unmodified template — no project principles have been ratified. **No gates to enforce; check passes vacuously.** Engineering defaults from feature 001 carried forward: component-per-concern with one public header, no speculative abstraction, `main` only wires components.

*Post-Phase-1 re-check*: one new component, zero new tasks/timers/buses. Still passes.

## Project Structure

### Documentation (this feature)

```text
specs/002-rtc-time-restore/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   ├── rtc-component-api.md   # rtc_time public header contract
│   └── status-api.md          # /api/status additions
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created by this command)
```

### Source Code (repository root)

Additions/changes to the existing 001 layout only:

```text
components/
├── rtc_time/                  # NEW: boot restore check + sync-time validity record + state
│   ├── CMakeLists.txt         #   injects BUILD_EPOCH compile definition (CMake string(TIMESTAMP))
│   ├── rtc_time.h             #   public API (see contracts/rtc-component-api.md)
│   ├── rtc_time.c             #   RTC_NOINIT validity record, CRC, plausibility check, state
│   └── test/                  #   Unity: record validation, bounds, state transitions
├── app_ctx/app_ctx.h          # MOD: app_time_source_t enum + time_source field + APP_EVT_TIME_RESTORED
├── wifi_mgr/wifi_mgr.c        # MOD: sntp_sync_cb() additionally calls rtc_time_mark_synced()
├── display/display.c          # MOD: time visibility driven by time_source != NONE (not time_synced)
└── web_server/handlers_mgmt.c # MOD: /api/status adds "time_source" (+ "time_last_sync")
main/main.c                    # MOD: rtc_time_restore() before task creation
sdkconfig.defaults             # MOD: pin CONFIG_ESP_SYSTEM_TIME_SOURCE to RTC + high-res timer
```

**Structure Decision**: The timekeeping concern gets its own component (`rtc_time`) following the 001 convention of one component per concern with a single public header. The SNTP→validity-record write stays in `wifi_mgr`'s existing sync callback (it already owns the SNTP lifecycle and posts `APP_EVT_TIME_SYNCED`), keeping the event graph flat. `main` calls `rtc_time_restore()` synchronously before starting tasks, so the display's first frame already has valid time whenever the RTC domain kept it.

## Complexity Tracking

No constitution violations to justify (constitution not ratified). One new component; no new FreeRTOS tasks, timers, buses, or storage. The external-RTC-chip alternative (DS3231 over I2C) was rejected in research.md R1 — the spec directs using the built-in RTC domain, which needs no driver at all.
