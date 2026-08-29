# Tasks: BMP280 Temperature/Pressure Sensor

**Input**: Design documents from `/specs/005-bmp280-sensor/`

**Prerequisites**: plan.md, spec.md, research.md (D1–D8), data-model.md, contracts/readings-api.md, quickstart.md

**Tests**: Included — the plan's test strategy (research D8) makes the compensation math, record codec, and API shapes test-gated (SC-003 regression, contract obligations §1–§4).

**Organization**: US1 = live pressure (MVP), US2 = pressure history, US3 = detection/fallback & probe-only regression. The sensor foundation (driver, state, detection) is Phase 2 since all three stories depend on it.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: US1 = live pressure, US2 = history, US3 = detection/fallback

## Phase 1: Setup

- [X] T001 Verify green baseline: `python tools/test_hw_emulator.py`, `python tools/check_i18n.py`, and `idf.py build` all pass before changes.

---

## Phase 2: Foundational (Blocking Prerequisites)

- [X] T002 Extend `components/app_ctx/app_ctx.h`: add `pressure_reading_t` (value_hpa, valid, updated_at_ms), `sensor_kind_t` enum (`SENSOR_NONE`, `SENSOR_DS18B20`, `SENSOR_BMP280`), and `app_state.pressure` + `app_state.sensor_kind` fields (data-model "Pressure reading" / "Sensor configuration").
- [X] T003 [P] Create BMP280 driver `components/sensor/bmp280.h` + `components/sensor/bmp280.c`: `bmp280_detect()` (probe 0x76 then 0x77 on the given `i2c_master` bus, verify chip-id 0x58, read 24-byte calibration), `bmp280_configure()` (osrs_t ×2, osrs_p ×16, IIR 4, normal mode, standby 1 s — research D3), `bmp280_read()` (6-byte burst 0xF7–0xFC + compensation), and the pure compensation functions `bmp280_compensate_t/p` per datasheet (exposed for tests, data-model driver interface).
- [X] T004 [P] Add Unity test `components/sensor/test/test_bmp280.c` + `components/sensor/test/CMakeLists.txt`: datasheet §3.11.3 worked example (raw_t=519888, raw_p=415148 with the example calibration → 25.08 °C, ~1006.53 hPa) plus an out-of-range clamp case.
- [X] T005 Update `components/sensor/CMakeLists.txt`: add `bmp280.c` to SRCS, add `esp_driver_i2c` to REQUIRES.
- [X] T006 Rework `components/sensor/sensor.c` (+ doc note in `sensor.h`): create the I2C master bus (SCL=GPIO22, SDA=GPIO21, 400 kHz, internal pull-ups — research D2), run boot detection (BMP280 → DS18B20 → none, set `app_state.sensor_kind`), and in the task loop read the active sensor: BMP280 mode updates both readings from one burst (validity ranges: temp −40..85 °C, pressure 300..1100 hPa), failure invalidates both + same-sensor re-init retry, never falls back to probe (research D6); DS18B20 mode unchanged, pressure permanently invalid. Post one `APP_EVT_READING_UPDATED` per cycle. Depends on T002, T003, T005.

**Checkpoint**: firmware compiles; sensor task populates temperature+pressure state per fitting.

---

## Phase 3: User Story 1 — See atmospheric pressure at a glance (Priority: P1) 🎯 MVP

**Goal**: Live pressure on the LCD and management page from a BMP280-equipped station.

**Independent Test**: quickstart §2.1–2.4 (emulator scenarios) + contract §1 status-shape tests; on hardware, quickstart §3.1–3.2.

- [X] T007 [US1] Update `/api/status` in `components/web_server/handlers_mgmt.c`: always emit `pressure_hpa` (%.1f), `pressure_valid`, and `sensor` (`"bmp280"|"ds18b20"|"none"` from `app_state.sensor_kind`); grow the JSON buffer 896 → 1024 (contract §1). Depends on T002.
- [X] T008 [P] [US1] Add pressure to the LCD: `ui_set_pressure(float hpa, bool valid)` in `components/display/ui.c`/`ui.h` (mid-size label under the temperature area, text `1013.2 hPa`, `---` when invalid, hidden when `sensor_kind != SENSOR_BMP280`); call it from the reading-event path in `components/display/display.c`. Depends on T002.
- [X] T009 [P] [US1] Add a pressure row to Current Readings in `components/web_server/www/mgmt/mgmt.html`: `<div class="reading-row" id="pressure-row">` with value span `press-val`, fixed unit `hPa`, label via `data-i18n="mgmt_label_pressure"`; hidden by default (contract §4).
- [X] T010 [US1] Render pressure in `components/web_server/www/mgmt/mgmt.js` `renderStatus`: show the row only when `s.sensor === 'bmp280'`; value `s.pressure_valid ? s.pressure_hpa.toFixed(1) : '---'` (contract §4). Depends on T009.
- [X] T011 [US1] Add i18n keys `mgmt_label_pressure` and `mgmt_th_pressure` to all four packs `components/web_server/www/i18n/{en,de,fr,uk}.json` (en: "Pressure"; de: "Luftdruck"; fr: "Pression"; uk: "Тиск") — data-model i18n table.
- [X] T012 [P] [US1] Extend `tools/hw_emulator.py`: deterministic `pressure_at(epoch)` (≈1013 ± 8 hPa slow sinusoid + small noise); scenario field `"sensor"` ∈ `bmp280|ds18b20|none` (default `bmp280`, add validator + CLI default); `/api/status` emits the three new fields per contract §1 (`none` also forces `temperature_valid` false); `/emu` control page gains the sensor selector.
- [X] T013 [US1] Add emulator tests to `tools/test_hw_emulator.py`: status shape for each `sensor` scenario value (fields always present, correct `pressure_valid`/`sensor` combos, contract §1); `pressure_at` determinism/range. Depends on T012.
- [ ] T014 [US1] Validate US1: `python tools/test_hw_emulator.py` green; browser walk quickstart §2.1, §2.2, §2.7 against the emulator.

**Checkpoint**: live pressure end-to-end (emulator-verified) — MVP.

---

## Phase 4: User Story 2 — Review pressure history (Priority: P2)

**Goal**: Pressure in the 5-minute/3-month history: JSON records, CSV, history table; old records stay readable.

**Independent Test**: contract §2/§3 tests against the emulator; quickstart §2.5–2.6; on hardware §3.7.

- [X] T015 [US2] Extend `components/history/history.c` + `history.h`: v2 12-byte packed record (`press_deci` u16 = hPa×10, flags bit1 = pressure recorded, reserved byte, CRC over bytes 0..10, static-asserted); write new samples as v2 into `YYYYMM.bi2`; `record_sample` takes pressure + validity from `app_state.pressure`; reader dispatches record size by extension (`.bin`=8/v1, `.bi2`=12/v2) in both `history_query` and the purge/count scan; `history_cb_t` gains `float pressure_hpa` (NAN when absent) — update signature and header docs (research D4, data-model record v2).
- [X] T016 [US2] Update `components/history/test/test_history.c`: v2 encode/decode round-trip incl. CRC and flags-bit1 semantics; a v1 8-byte record still decodes with pressure absent; pressure range encode check. Depends on T015.
- [X] T017 [US2] Update history streaming in `components/web_server/handlers_mgmt.c`: JSON callback emits `"pressure":<%.1f|null>`; CSV header becomes `timestamp_iso8601,temperature_c,pressure_hpa` with empty cell for NAN (contract §2–§3). Depends on T015.
- [X] T018 [US2] History table pressure column: add `<th data-i18n="mgmt_th_pressure">` (hidden by default) to `components/web_server/www/mgmt/mgmt.html` and, in `components/web_server/www/mgmt/mgmt.js`, show the column and per-row cell only when loaded data contains ≥1 non-null pressure (contract §4). Depends on T009–T011.
- [X] T019 [P] [US2] Extend `tools/hw_emulator.py` history: records/CSV carry pressure from `pressure_at()` only while scenario `sensor == "bmp280"` (JSON `null` / empty CSV cell otherwise), per contract §2–§3.
- [X] T020 [US2] Add emulator tests to `tools/test_hw_emulator.py`: history JSON pressure number-vs-null, CSV 3-column header + blank cells, existing 2 columns unchanged (contract obligations 2–3). Depends on T019.
- [ ] T021 [US2] Validate US2: suite green; browser walk quickstart §2.5–2.6.

**Checkpoint**: pressure history end-to-end; pre-upgrade data intact.

---

## Phase 5: User Story 3 — The right sensor is picked automatically (Priority: P3)

**Goal**: Detection/fallback proven; probe-only stations show zero regression.

**Independent Test**: quickstart §2.3–2.4 (emulator) and §3.3–3.6 (hardware).

- [X] T022 [US3] Add probe-only regression tests to `tools/test_hw_emulator.py` (SC-003, contract obligation 4): with scenario `sensor:"ds18b20"`, status reports `pressure_valid:false`/`sensor:"ds18b20"`, history JSON pressures are all null, CSV pressure cells all empty; with `sensor:"none"`, temperature also invalid; switching scenarios back restores pressure.
- [ ] T023 [US3] Validate US3: suite green; browser walk quickstart §2.3–2.4 (no pressure artifacts in either non-BMP280 fitting).

**Checkpoint**: all three stories functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T024 Full automated gate: `python tools/test_hw_emulator.py`, `python tools/check_i18n.py` (0 discrepancies with the new keys), and `idf.py build` all green.
- [ ] T025 On-device validation per quickstart §3 (BMP280 boot, warm test, probe-only boot, no-sensor boot, mid-run disconnect, plug-and-reboot, history over time) — requires hardware.

---

## Dependencies & Execution Order

- **Phase 2**: T002 first; T003 ∥ T004; T005 after T003; T006 last (needs T002+T003+T005).
- **US1**: T007/T008 after T002; T009 → T010; T011 anytime; T012 ∥ firmware tasks; T013 after T012; T014 last.
- **US2**: T015 → (T016 ∥ T017); T018 after T009–T011; T019 ∥ T015; T020 after T019; T021 last.
- **US3**: T022 after T012/T019; T023 last. US3's firmware behavior is built in T006; this phase proves it.
- **Polish**: T024 after all code tasks; T025 requires hardware.

### Parallel opportunities

- Phase 2: T003 (driver) ∥ T004 (its test skeleton); T002 alongside both.
- US1: firmware chain (T007/T008) ∥ web chain (T009–T011) ∥ emulator chain (T012–T13).
- US2: T016 ∥ T017 after T015; T019 independent of firmware history work.
- Merge hotspots: `handlers_mgmt.c` (T007, T017), `mgmt.js`/`mgmt.html` (T009/T010/T018), `test_hw_emulator.py` (T013/T020/T022) — keep those sequential within each file.

## Implementation Strategy

MVP = Phases 1–3 (live pressure, emulator-verified). US2 adds history, US3 adds regression proof; each checkpoint is independently shippable. On-device steps (T025) close SC-001/002/005/006 on real hardware.
