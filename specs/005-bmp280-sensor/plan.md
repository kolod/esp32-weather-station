# Implementation Plan: BMP280 Temperature/Pressure Sensor

**Branch**: `005-bmp280-sensor` | **Date**: 2026-07-14 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/005-bmp280-sensor/spec.md`

## Summary

Add BMP280 support over I2C (**SCL=GPIO22, SDA=GPIO21** — user-specified) with startup auto-detection: BMP280 present → it supplies temperature and pressure; otherwise the existing DS18B20 probe supplies temperature only; neither → readings unavailable. Pressure becomes a first-class reading through the whole chain: shared state → LCD → `/api/status` → 5-minute history (new on-disk record version, old records stay readable) → history JSON/CSV → localized management page. The BMP280 driver is written in-component against ESP-IDF's `i2c_master` API (~150 lines incl. Bosch compensation math, which is pure logic and unit-testable against the datasheet's worked example) — no new dependencies. The hardware emulator gains pressure simulation and a sensor-fitting scenario switch so all web-facing behavior is testable without hardware.

## Technical Context

**Language/Version**: C (C17, `-std=gnu23` toolchain) on ESP-IDF v6.0.2; vanilla JS/HTML (embedded web UI); Python 3.11+ (emulator/tests)

**Primary Dependencies**: ESP-IDF `esp_driver_i2c` (`i2c_master.h`, new bus API — first I2C use in the project); existing `espressif/ds18b20` managed component stays for the probe path. **No new managed components** (BMP280 driver implemented in-house, research D1).

**Storage**: History on LittleFS (`/storage/history/YYYYMM.bin`, packed 8-byte records). Extended with a v2 12-byte record in new files; v1 files remain readable (research D4).

**Testing**: Unity component tests (BMP280 compensation math vs datasheet example, v2 history record codec/CRC); Python unittest suite against the emulator (status/history/CSV shapes, sensor-fitting scenarios); `tools/check_i18n.py` (new pressure labels); `idf.py build` gate; on-device quickstart.

**Target Platform**: ESP32 (BMP280 at I2C address 0x76 or 0x77, SCL=GPIO22, SDA=GPIO21); evergreen browsers for UI.

**Project Type**: Embedded firmware + embedded web UI + Python dev tooling (existing structure).

**Performance Goals**: Pressure and temperature refreshed every 5 s (existing cadence); pressure visible ≤10 s after power-on (SC-001); no change to the 1-write-per-hour flash budget.

**Constraints**: GPIO21/22 are free (probe=GPIO27 1-Wire, display on SPI); history writes stay batched hourly; `api_status` JSON built in a fixed buffer (currently 896 B — must grow for the new fields); localization must stay checker-clean (56→~60 keys × 4 languages).

**Scale/Scope**: 1 new driver, 1 new I2C bus, ~6 components touched, 2–3 new status fields, 1 record-format version, ~4 new i18n keys.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

`.specify/memory/constitution.md` is an unfilled template — generic engineering gates applied:

| Gate | Status | Notes |
|------|--------|-------|
| No new dependencies without justification | PASS | In-house BMP280 driver over IDF's own `esp_driver_i2c`; no new managed components (D1). |
| Backward compatibility of persisted data | PASS | v1 history files remain readable; v2 used only for new files (D4). |
| Existing behavior preserved (probe-only fleet) | PASS | DS18B20 path untouched when no BMP280 detected; SC-003 enforced by tests. |
| New logic gets tests | PASS | Compensation math + v2 codec in Unity; API/UI shapes + fittings in emulator suite. |

**Post-design re-check (after Phase 1)**: PASS — no speculative abstraction; one driver, one record version bump, additive API fields only.

## Project Structure

### Documentation (this feature)

```text
specs/005-bmp280-sensor/
├── plan.md              # This file
├── research.md          # Phase 0: decisions D1–D8
├── data-model.md        # Phase 1: readings, sensor config, record v2, JSON shapes
├── quickstart.md        # Phase 1: emulator + on-device validation
├── contracts/
│   └── readings-api.md  # Phase 1: status/history/CSV contract changes
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
components/
├── sensor/
│   ├── sensor.c               # MODIFY: startup detection (BMP280 → DS18B20 → none),
│   │                          #         task reads active sensor, updates temp+pressure
│   ├── sensor.h               # MODIFY: doc update (detection behavior)
│   ├── bmp280.c               # NEW: minimal driver — chip-id probe, calibration read,
│   │                          #      mode config, burst read, Bosch compensation
│   │                          #      (pure functions, separately testable)
│   ├── bmp280.h               # NEW: driver API + compensation functions exposed for tests
│   ├── CMakeLists.txt         # MODIFY: add bmp280.c; REQUIRES esp_driver_i2c
│   └── test/                  # NEW: test_bmp280.c (compensation vs datasheet example)
├── app_ctx/app_ctx.h          # MODIFY: add pressure_reading_t + app_state.pressure
├── history/
│   ├── history.c              # MODIFY: v2 12-byte record (adds pressure), write v2,
│   │                          #         read v1+v2 (record size per file version), purge both
│   ├── history.h              # MODIFY: history_cb_t gains pressure (NAN = not recorded)
│   └── test/test_history.c    # MODIFY: v2 codec/CRC cases; v1 still decodes
├── display/
│   ├── ui.c / ui.h            # MODIFY: pressure label (value + "hPa"), ui_set_pressure()
│   └── display.c              # MODIFY: push pressure from app_state on reading events
└── web_server/
    ├── handlers_mgmt.c        # MODIFY: /api/status adds pressure fields + active sensor;
    │                          #         history JSON/CSV callbacks add pressure column
    └── www/
        ├── mgmt/mgmt.html     # MODIFY: pressure row in Current Readings; history table col
        ├── mgmt/mgmt.js       # MODIFY: render pressure (value/unavailable), history col
        └── i18n/{en,de,fr,uk}.json  # MODIFY: mgmt_pressure & related keys (4 langs)

main/                          # unchanged (sensor_start() signature stays)

tools/
├── hw_emulator.py             # MODIFY: pressure_at() simulation; status/history/CSV fields;
│                              #         scenario field "sensor": bmp280|ds18b20|none
└── test_hw_emulator.py        # MODIFY: pressure in status/history/CSV; fitting scenarios;
                               #         probe-only regression shape (SC-003)
```

**Structure Decision**: The BMP280 driver lives inside the existing `sensor` component (it is the sensor concern's private detail; no other component talks to it). The I2C master bus is created by the sensor component since it is the only I2C user today; if a future feature needs the bus it can be lifted into its own component then (YAGNI). All other changes extend existing files along the exact chain temperature already flows through — no new components.

## Complexity Tracking

No constitution violations — table not required.
