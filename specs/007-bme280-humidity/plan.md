# Implementation Plan: BME280 Humidity Support & Quadrant Main Screen

**Branch**: `007-bme280-humidity` | **Date**: 2026-08-29 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/007-bme280-humidity/spec.md`

## Summary

Extend feature 005's Bosch environmental-sensor path to recognise the **BME280** (chip-id `0x60`) in addition to the BMP280 (`0x58`) on the same I2C bus, and to read its **relative humidity** channel. Boot-time detection gains one rung: BME280 → BMP280 → DS18B20 → none. When a BME280 is active, humidity flows through the whole chain already built for pressure: shared state → LCD → `/api/status` → 5-minute history → history JSON/CSV → localised management page.

Humidity needs **no new on-disk record version**: the v2 history record (feature 005) reserved two bytes "for one future field (e.g. humidity)" — this feature spends them as `hum_centi` (%RH × 100) plus one flags bit. Old v2 records (reserved = 0, bit clear) read back as "humidity absent"; v1 `.bin` files are unchanged.

The BME280 protocol delta over the existing driver is small: the temperature and pressure compensation formulas are **byte-identical** to the BMP280; only the extra calibration block, the `ctrl_hum` register, an 8-byte burst (vs 6), and one humidity compensation function are new. The driver is therefore extended in place (`components/sensor/bmp280.{c,h}`), not duplicated.

In parallel, the LCD main screen is rebuilt as a fixed four-quadrant layout (time TL, temperature TR, pressure BL, humidity BR) with the WiFi glyph centred. The layout is identical on every station; quadrants with no reading show a dashed placeholder. This supersedes feature 005's "hide the pressure label when no BMP280 is fitted".

The hardware emulator gains a `bme280` fitting and a `humidity_at()` generator so every web surface is testable without hardware.

## Technical Context

**Language/Version**: C (C17, `-std=gnu23` toolchain) on ESP-IDF v6.0.2, `esp32` target; vanilla JS/HTML (embedded web UI); Python 3.11+ (emulator/tests)

**Primary Dependencies**: existing only — `esp_driver_i2c` (`i2c_master.h`, feature 005), `espressif/ds18b20` + `onewire_bus` (probe path), LVGL via `esp_lvgl_port` (display). **No new managed components** (BME280 support added to the in-house driver, research D1).

**Storage**: History on LittleFS. v2 12-byte record (`YYYYMM.bi2`) reused unchanged in size/name — the 2 reserved bytes become `hum_centi`, one spare `flags` bit marks "humidity recorded" (research D3). v1 `.bin` and existing v2 `.bi2` files stay readable.

**Testing**: Unity component tests (BME280 humidity compensation vs a reference vector; v2 record humidity codec/CRC; v1 + legacy-v2 still decode); Python unittest against the emulator (status/history/CSV shapes per fitting, bme280 scenario, regression that non-BME280 fittings show no humidity artefacts); `tools/check_i18n.py` (2 new keys × 4 languages); `idf.py build` zero-warning gate; on-device quickstart (screen layout + three fittings + mid-run disconnect).

**Target Platform**: ESP32-D0WDQ6; BME280/BMP280 at I2C 0x76/0x77 on SCL=GPIO22 / SDA=GPIO21 (feature 005 wiring, unchanged); ST7789 250×135 landscape LCD; evergreen browsers for the UI.

**Project Type**: Embedded firmware + embedded web UI + Python dev tooling (existing structure).

**Performance Goals**: humidity refreshed on the existing 5 s cadence; visible on screen and `/api/status` ≤10 s after power-on (SC-001); flash-write budget unchanged (still one history flush per hour, record size unchanged).

**Constraints**: no new GPIO (reuses the feature-005 I2C bus); `/api/status` JSON built in a fixed stack buffer — currently `char buf[1024]`, grows to 1152 B for `humidity_pct` / `humidity_valid`; LCD uses exactly two LVGL faces — Montserrat 28 for values, Montserrat 14 for labels (`sdkconfig.defaults` change, research D5); i18n must stay checker-clean (61 → 63 keys × 4 languages).

**Scale/Scope**: 1 driver extended (no new files in `components/sensor/` except test cases), +1 `sensor_kind_t` enum value, +1 shared-state reading struct, +2 `/api/status` fields, +1 history JSON field, +1 CSV column, +2 i18n keys, full LCD `ui.c` re-layout (~1 file), ~6 components touched.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Checked against `.specify/memory/constitution.md` v1.0.0.

| Principle | Status | Notes |
|-----------|--------|-------|
| I. ESP-IDF Component Architecture | PASS | No new component. Changes extend `sensor`, `display`, `history`, `web_server`, `app_ctx` along the exact chain temperature/pressure already flow through. Cross-component data stays in `app_state` (new `humidity_reading_t`), one existing event (`APP_EVT_READING_UPDATED`). |
| II. Hardware Abstraction Layer | PASS | BME280 register/burst detail stays entirely in `bmp280.c`. No pin literals outside the driver; I2C bus is the feature-005 one, no new pins. Buttons untouched. |
| III. Build Integrity (NON-NEGOTIABLE) | PASS (gate) | `idf.py build` verified clean (zero warnings). The one `sdkconfig.defaults` change (enable Montserrat 28, drop unused 20/36/48 — research D5) carries a verified rebuild. |
| IV. Embedded Resource Discipline | PASS | `humidity_reading_t` adds 16 B to `app_state`. History record size **unchanged** (spends the reserved bytes) — no extra flash wear, retention unchanged. JSON buffer +128 B stack. No new task, no ISR allocation. Sensor cadence unchanged (5 s). |
| V. Network & Security Standards | PASS | No change to WiFi, AP fallback, HTTPS, NVS, or OTA. Only additive read-only JSON fields and static-asset text. |
| Hardware Platform Standards | PASS | The constitution's platform section already names "BMP280/BME280 … (BME280 only) relative humidity" as the sensor contract — this feature realises it. |
| Development Workflow & Git Standards | PASS | Feature on branch `007-bme280-humidity`, spec-first (`specs/007-*`), Conventional Commits, business-logic tests (humidity compensation, record codec) runnable via `idf.py -C components/sensor/test build`. |

**Pre-existing note (not introduced here)**: the constitution's platform text says the sensor is "via SPI"; feature 005 deliberately chose I2C (its research D2, user-supplied pins). This plan follows the shipped I2C implementation. Flagged for a constitution PATCH, out of scope for this feature.

**Post-design re-check (after Phase 1)**: PASS — no new abstraction; driver extended not forked, record version not bumped, API fields additive, one screen file re-laid-out.

## Project Structure

### Documentation (this feature)

```text
specs/007-bme280-humidity/
├── plan.md              # This file
├── research.md          # Phase 0: decisions D1–D7
├── data-model.md        # Phase 1: humidity reading, sensor_kind, v2 record reuse, JSON shapes, screen layout
├── quickstart.md        # Phase 1: emulator + on-device validation
├── contracts/
│   ├── readings-api.md  # Phase 1: /api/status, /api/history, /api/history.csv deltas
│   └── screen-layout.md # Phase 1: quadrant geometry + ui.h contract
├── checklists/
│   └── requirements.md  # spec quality checklist (done)
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
components/
├── sensor/
│   ├── bmp280.h               # MODIFY: humidity calib fields; handle gains `has_humidity`;
│   │                          #         bmp280_read() gains `float *hum_pct` (NULL = don't care);
│   │                          #         expose bme280_compensate_h() for tests
│   ├── bmp280.c               # MODIFY: detect chip-id 0x60 (BME280) as well as 0x58;
│   │                          #         read humidity calib (0xA1, 0xE1..0xE7); write ctrl_hum
│   │                          #         before ctrl_meas; 8-byte burst; humidity compensation
│   ├── sensor.c               # MODIFY: detection ladder BME280→BMP280→DS18B20→none;
│   │                          #         read_bmp280() also fills app_state.humidity;
│   │                          #         humidity range-guard 0–100 %RH
│   ├── sensor.h               # MODIFY: doc — detection order + humidity
│   ├── CMakeLists.txt         # unchanged (no new sources; test dir already registered)
│   └── test/
│       └── test_bmp280.c      # MODIFY: BME280 humidity compensation reference-vector case(s)
├── app_ctx/
│   └── app_ctx.h              # MODIFY: humidity_reading_t + app_state.humidity;
│                              #         sensor_kind_t gains SENSOR_BME280 = 3
├── history/
│   ├── history.c              # MODIFY: reinterpret v2 reserved[2] as hum_centi; flags bit2;
│   │                          #         write humidity when valid; decode → NAN when bit2 clear
│   ├── history.h              # MODIFY: history_cb_t gains `float humidity_pct` (NAN = absent)
│   └── test/test_history.c    # MODIFY: v2 humidity encode/decode/CRC; legacy v2 (reserved=0)
│                              #         and v1 still decode with humidity absent
├── display/
│   ├── ui.h                   # MODIFY: ui_set_humidity(); ui_set_pressure() loses `present`;
│   │                          #         doc the four-quadrant contract
│   ├── ui.c                   # MODIFY: rebuild ui_init() as 4 quadrants + centred WiFi glyph;
│   │                          #         placeholder rendering for unavailable quadrants
│   └── display.c              # MODIFY: read app_state.humidity; call ui_set_humidity();
│                              #         drop sensor-kind arg from ui_set_pressure()
└── web_server/
    ├── handlers_mgmt.c        # MODIFY: /api/status +humidity_pct +humidity_valid, sensor enum
    │                          #         +"bme280", buf[1024]→[1152]; history JSON +"humidity";
    │                          #         CSV +humidity_pct column; hist_*_cb signatures
    └── www/
        ├── mgmt/mgmt.html     # MODIFY: humidity row in Current Readings; history <th> humidity
        ├── mgmt/mgmt.js       # MODIFY: render humidity (bme280 only); history humidity column
        └── i18n/{en,de,fr,uk}.json  # MODIFY: mgmt_label_humidity, mgmt_th_humidity (4 langs)

tools/
├── hw_emulator.py            # MODIFY: SENSOR_KINDS +"bme280"; humidity_at(); status
│                             #         humidity_pct/valid; pressure_valid also for bme280;
│                             #         history + CSV humidity; BOOTLOG_SENSOR_LINES["bme280"];
│                             #         /emu control + --sensor choices
└── test_hw_emulator.py       # MODIFY: humidity in status/history/CSV; bme280 scenario;
                              #         regression: bmp280/ds18b20/none show no humidity fields-as-values

main/                          # unchanged
```

**Structure Decision**: The BME280 is the same driver concern as the BMP280 (same vendor, bus, address, and — for T/P — the same compensation math), so `bmp280.{c,h}` is **extended in place** rather than forked into `bme280.c`; the file name is kept to avoid churning `CMakeLists.txt` and the Unity test target. Every other change rides the pressure chain that feature 005 already carved. The only structurally new work is the LCD `ui.c` re-layout, which stays inside the `display` component per Principle II.

## Complexity Tracking

No constitution violations — table not required.
