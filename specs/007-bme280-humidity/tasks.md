# Tasks: BME280 Humidity Support & Quadrant Main Screen

**Input**: Design documents from `/specs/007-bme280-humidity/`

**Prerequisites**: plan.md, spec.md, research.md (D1–D7), data-model.md, contracts/readings-api.md, contracts/screen-layout.md, quickstart.md

**Tests**: Included — research D7 and the contract test obligations make the humidity compensation, the record codec, and the API/emulator shapes test-gated (SC-003 regression, contract obligations 1–4).

**Organization**: US1 = live humidity (MVP), US2 = humidity history, US3 = the four-quadrant screen, US4 = autodetect ladder & non-BME280 regression. The sensor foundation (driver extension, shared state, detection) is Phase 2 since every story depends on it.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: US1 = live humidity, US2 = history, US3 = quadrant screen, US4 = autodetect

## Path Conventions

ESP-IDF firmware components under `components/`, embedded web UI under `components/web_server/www/`, Python dev tooling under `tools/`. No `src/` root.

---

## Phase 1: Setup

- [X] T001 Verify green baseline before changes: `python tools/test_hw_emulator.py`, `python tools/check_i18n.py` (expect `61 keys x 4 languages`, 0 discrepancies), and `idf.py build` all pass.

---

## Phase 2: Foundational (Blocking Prerequisites)

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [X] T002 Extend `components/app_ctx/app_ctx.h`: add `humidity_reading_t { float value_pct; bool valid; int64_t updated_at_ms; }`, add `SENSOR_BME280 = 3` to `sensor_kind_t` (after `SENSOR_BMP280`), and add `app_state.humidity` to `app_state_t` (data-model "Humidity reading" / "Sensor configuration").
- [X] T003 Extend the Bosch driver `components/sensor/bmp280.h` + `components/sensor/bmp280.c` for the BME280 (research D1/D2, data-model "History record" section not applicable here):
  - `bmp280_detect()` accepts chip-id `0x58` (BMP280) **or** `0x60` (BME280); store `bool has_humidity` on the handle; when both 0x76 and 0x77 hold Bosch sensors prefer the `0x60` one.
  - When `has_humidity`: also read humidity calibration (`dig_H1` at `0xA1`; `dig_H2..dig_H6` packed across `0xE1..0xE7`) and parse into new `bmp280_calib_t` fields.
  - `bmp280_configure()`: write `ctrl_hum` (`0xF2`, humidity oversampling ×1) **before** `ctrl_meas` when `has_humidity`.
  - `bmp280_read(dev, float *temp_c, float *press_hpa, float *hum_pct)`: burst-read 8 bytes (`0xF7..0xFE`) when `has_humidity` (6 otherwise); `hum_pct` may be `NULL`; leave `*hum_pct` untouched / caller-ignored for a BMP280.
  - Add pure `int32_t bme280_compensate_h(int32_t raw_h, const bmp280_calib_t *c, int32_t t_fine)` (Bosch `BME280_compensate_H_int32`, returns %RH in Q22.10 or the datasheet's fixed-point scale — document the unit in the header), exposed for tests.
  - `bmp280_compensate_t` / `bmp280_compensate_p` unchanged.
- [X] T004 [US4] Add BME280 humidity cases to `components/sensor/test/test_bmp280.c` (research D6): a fixed calibration set + several raw humidity codes; assert the fixed-point `bme280_compensate_h` agrees with the datasheet double-precision reference formula within 1 %RH; assert clamp at 0 %RH and 100 %RH. Depends on T003.
- [X] T005 Rework detection + humidity read in `components/sensor/sensor.c` (and update the doc comment in `components/sensor/sensor.h`): boot detection ladder BME280 → BMP280 → DS18B20 → none, set `app_state.sensor_kind` (research D2); in `read_bmp280()` also fill `app_state.humidity` from the third `bmp280_read` out-param with a 0–100 %RH range guard (FR-007); a failed read invalidates temperature, pressure **and** humidity and retries the same sensor (no rung fallback mid-run); post one `APP_EVT_READING_UPDATED` per cycle as today. Depends on T002, T003.

**Checkpoint**: firmware compiles; sensor task populates temperature + pressure + humidity state for a BME280, and behaves exactly as feature 005 for BMP280 / DS18B20 / none.

---

## Phase 3: User Story 1 — See humidity at a glance (Priority: P1) 🎯 MVP

**Goal**: Live relative humidity on the LCD (in its quadrant) and on the management page from a BME280-equipped station.

**Independent Test**: quickstart §2.1–2.2 (emulator `bme280` scenario) + contract §1 status-shape tests; on hardware, quickstart §3.1–3.2.

- [X] T006 [US1] Update `/api/status` in `components/web_server/handlers_mgmt.c` (contract §1): always emit `humidity_pct` (`%.1f`) and `humidity_valid`; extend the `sensor_str[]` array to `{"none","ds18b20","bmp280","bme280"}` and its bounds check to `< 4`; set `pressure_valid` true for `SENSOR_BMP280` **or** `SENSOR_BME280`; grow `char buf[1024]` → `char buf[1152]`. Depends on T002.
- [X] T007 [US1] Rebuild the LCD main screen in `components/display/ui.c` + `components/display/ui.h` per [contracts/screen-layout.md](./contracts/screen-layout.md): four fixed quadrant containers (time TL, temperature TR, pressure BL, humidity BR) with the WiFi glyph at `LV_ALIGN_CENTER`; add `void ui_set_humidity(float value_pct, bool valid)`; change `ui_set_pressure` signature to `(float value_hpa, bool valid)` (drop `present`); value/sub-label fonts and placeholder strings (`--:--`, `---`, `--- hPa`, `--- %`) per the contract table; no quadrant ever hidden. Depends on T002.
- [X] T008 [US1] Update `components/display/display.c` `display_event_handler`: read `app_state.humidity` under `app_state_mutex`; call `ui_set_humidity(h.value_pct, h.valid)`; change the `ui_set_pressure(...)` call to the new 2-arg form (drop the `sensor == SENSOR_BMP280` argument). Depends on T007.
- [X] T009 [P] [US1] Add a humidity row to Current Readings in `components/web_server/www/mgmt/mgmt.html`: `<div class="reading-row hidden" id="humidity-row">` with badge `data-i18n="mgmt_label_humidity"`, value span `id="hum-val"`, fixed unit `%` (mirror the existing `pressure-row` markup). Depends on nothing in this phase.
- [X] T010 [US1] Render humidity in `components/web_server/www/mgmt/mgmt.js` `renderStatus`: show `#humidity-row` only when `s.sensor === 'bme280'`; value `s.humidity_valid ? s.humidity_pct.toFixed(0) : '---'`; also widen the existing pressure-row condition to `s.sensor === 'bmp280' || s.sensor === 'bme280'` (contract §4). Depends on T009.
- [X] T011 [P] [US1] Add i18n keys `mgmt_label_humidity` and `mgmt_th_humidity` to all four packs `components/web_server/www/i18n/{en,de,fr,uk}.json` (en: `Humidity`; de: `Luftfeuchte`; fr: `Humidité`; uk: `Вологість`) — data-model i18n table.
- [X] T012 [P] [US1] Extend `tools/hw_emulator.py`: `SENSOR_KINDS = ("bme280","bmp280","ds18b20","none")` (add validator entry, keep `--sensor` default `bmp280`); deterministic `humidity_at(epoch)` (≈50 ± 20 %RH ~1-day sinusoid + small per-minute noise, clamped 0–100); `status()` emits `humidity_pct` / `humidity_valid` (`sensor == "bme280"` and `sensor_valid`), and `pressure_valid` widened to `sensor in ("bmp280","bme280")` (contract §1); add `bme280` to the `/emu` control page sensor `<select>`.
- [X] T013 [US1] Add emulator status tests to `tools/test_hw_emulator.py`: for each `sensor` value, `humidity_pct` + `humidity_valid` always present with the correct combo; `pressure_valid` true for both Bosch fittings; `humidity_at` determinism/range (contract obligation 1). Depends on T012.
- [X] T014 [US1] Validate US1: `python tools/test_hw_emulator.py` green; browser walk quickstart §2.1, §2.2, §2.8 against `python tools/hw_emulator.py --sensor bme280`.

**Checkpoint**: live humidity end-to-end on screen + web (emulator-verified) — MVP.

---

## Phase 4: User Story 2 — Review humidity history (Priority: P2)

**Goal**: Humidity in the 5-minute / 3-month history — JSON records, CSV, history table — with pre-007 records still readable.

**Independent Test**: contract §2/§3 tests against the emulator; quickstart §2.6–2.7; on hardware §3.9.

- [X] T015 [US2] Extend `components/history/history.c` + `components/history/history.h` (research D3, data-model "History record v2"): in `hist_record_v2_t` rename `reserved[2]` → `uint16_t hum_centi`; add `flags` bit2 = "humidity recorded"; keep `sizeof == 12`, file name `.bi2`, and CRC over bytes `[0..10]` unchanged (re-check the `_Static_assert`). `record_sample()` takes humidity + validity from `app_state.humidity`, encodes `hum_centi = round(pct*100)` and sets bit2 only when valid and in 0–100 %RH. Decode → `NAN` when bit2 clear (covers pre-007 `.bi2` and all `.bin`). Extend `history_cb_t` to `(uint32_t epoch, float temp_c, float pressure_hpa, float humidity_pct, void *ctx)`.
- [X] T016 [P] [US2] Update `components/history/test/test_history.c`: v2 record humidity encode/decode round-trip incl. CRC and flags-bit2 semantics; a legacy v2 record with the two bytes zeroed decodes with humidity absent; a v1 8-byte record still decodes with pressure **and** humidity absent; humidity range/rounding check. Depends on T015.
- [X] T017 [US2] Update history streaming in `components/web_server/handlers_mgmt.c`: `hist_json_cb` / `hist_csv_cb` gain the `float humidity_pct` param; JSON emits `"humidity":<%.1f|null>`; CSV header becomes `timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct` with an empty cell for `NAN` (contract §2–§3). Depends on T015.
- [X] T018 [US2] History table humidity column: add `<th class="hidden" id="th-humidity" data-i18n="mgmt_th_humidity">` to `components/web_server/www/mgmt/mgmt.html`, and in `components/web_server/www/mgmt/mgmt.js` show the column + per-row cell only when the loaded data has ≥1 non-null humidity (same rule as the pressure column). Depends on T009–T011, T017.
- [X] T019 [P] [US2] Extend `tools/hw_emulator.py` history: add `history_humidity_at(ts)` (value from `humidity_at` when the fitting active at `ts` was `bme280`, else `None`); `h_history` JSON records gain `"humidity"`; `h_history_csv` gains the 4th column; segment-walk (`sensor_at`) already handles a mid-stream fitting switch (contract §2–§3).
- [X] T020 [US2] Add emulator history tests to `tools/test_hw_emulator.py`: history JSON `humidity` number-vs-null across a `bme280`→`bmp280` scenario switch; CSV 4-column header + blank cells; first three columns byte-identical to feature 005 (contract obligations 2–3). Depends on T019.
- [X] T021 [US2] Validate US2: suite green; browser walk quickstart §2.6–2.7.

**Checkpoint**: humidity history end-to-end; pre-upgrade and feature-005 records intact.

---

## Phase 5: User Story 3 — Reorganised four-quadrant main screen (Priority: P2)

**Goal**: The LCD shows the same four corners + centred WiFi glyph on every station, with placeholders where a reading is unavailable.

**Independent Test**: quickstart §3.3–3.6 (three fittings + button isolation on hardware); emulator boot-log fitting lines for surfaced text.

- [X] T022 [US3] Review `components/display/ui.c` against every rule in [contracts/screen-layout.md](./contracts/screen-layout.md) and close gaps: quadrant alignment/offsets, value + sub-label fonts, the four placeholder strings, WiFi glyph centred with connected/other colour, and that `ui_set_time` still renders the LOCAL/UTC badge and `ui_set_temperature` the °C/°F badge **inside their quadrants**; confirm the °C/°F and LOCAL/UTC toggles update only their own quadrant (no shared label). (Builds on T007; no other file.)
- [ ] T023 [US3] Validate US3: on hardware per quickstart §3.3–3.6 (BME280 all quadrants populated; BMP280 → `--- %`; probe-only → `--- hPa` + `--- %`; no-sensor → all placeholders; WiFi glyph centred and tracking state; button isolation). If Montserrat 36 strings clip the centre glyph, apply the research-D5 fallback (`CONFIG_LV_FONT_MONTSERRAT_28=y`, verified rebuild, all four values at 28) and re-check. **BLOCKED: requires the T-Display device + sensors.** Code review vs contracts/screen-layout.md complete (T022); `idf.py build` clean.

**Checkpoint**: identical four-quadrant layout across all fittings (SC-007).

---

## Phase 6: User Story 4 — The right sensor is picked automatically (Priority: P3)

**Goal**: Detection ladder proven end-to-end; BMP280 / probe-only / no-sensor stations show zero humidity regression.

**Independent Test**: quickstart §2.3–2.5 (emulator) and §3.4–3.8 (hardware).

- [X] T024 [P] [US4] Extend `tools/hw_emulator.py` for the `bme280` fitting's surfaced text: add a `BOOTLOG_SENSOR_LINES["bme280"]` entry (`I (…) bmp280: BME280 found at 0x76` / `I (…) sensor: Sensor mode: BME280 (temperature + pressure + humidity)`); add `bme280` to the `--sensor` `argparse` `choices` and the `--sensor` help text.
- [X] T025 [US4] Add non-BME280 regression tests to `tools/test_hw_emulator.py` (SC-003, contract obligation 4): with `sensor` `"bmp280"` / `"ds18b20"` / `"none"`, `/api/status` never reports `humidity_valid:true` and history JSON/CSV carry no humidity value; switching back to `bme280` restores humidity; boot-log text for the `bme280` fitting mentions humidity. Depends on T024.
- [X] T026 [US4] Validate US4: suite green; browser walk quickstart §2.3–2.5 (no humidity artefacts in any non-BME280 fitting).

**Checkpoint**: all four stories functional.

---

## Phase 6b: Screen typography (user follow-up)

- [X] T030 Two type sizes only: `sdkconfig.defaults` enables `CONFIG_LV_FONT_MONTSERRAT_28` (+ pin 14) and drops the now-unused 20/36/48; `components/display/ui.c` uses `FONT_VALUE` (Montserrat 28) for every quadrant value and `FONT_LABEL` (Montserrat 14) for every label. `contracts/screen-layout.md` + research D5 updated; `idf.py build` re-verified clean.

---

## Phase 7: Polish & Cross-Cutting Concerns

- [X] T027 Full automated gate: `python tools/test_hw_emulator.py`, `python tools/check_i18n.py` (0 discrepancies, `63 keys x 4 languages`), and `idf.py build` (zero warnings) all green.
- [X] T028 [P] Update `README.md` hardware/sensor section: BME280 as the humidity-capable option, autodetect order BME280 → BMP280 → DS18B20, humidity on screen/web/history, and the four-quadrant screen (addresses the constitution Sync Impact Report's pending README note).
- [ ] T029 On-device validation per quickstart §3 (BME280 boot, humidity tracking, screen layout, BMP280 boot, probe-only boot, no-sensor boot, mid-run disconnect, plug-and-reboot both directions, history over time) — requires hardware; closes SC-001/002/005/006/007. **BLOCKED: requires the T-Display device + BME280/BMP280/DS18B20.**

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (T001)**: none.
- **Foundational (T002–T005)**: after Setup; blocks all stories. Order: T002 → T003 → (T004 ∥ T005). T004 is labelled US4 but tests T003's foundational API, so it lands with Phase 2.
- **US1 (T006–T014)**: after Foundational. T006/T007 after T002; T008 after T007; T009 → T010; T011 anytime; T012 ∥ firmware; T013 after T012; T014 last.
- **US2 (T015–T021)**: after Foundational (independent of US1 firmware, but T018 needs US1's web rows T009–T011). T015 → (T016 ∥ T017); T018 after T017 + T009–T011; T019 ∥ T015; T020 after T019; T021 last.
- **US3 (T022–T023)**: after US1's T007/T008 (the screen is built there); T022 → T023.
- **US4 (T024–T026)**: after US1's T012 and US2's T019 (emulator surfaces to assert against); T024 → T025 → T026.
- **Polish (T027–T029)**: T027 after all code tasks; T028 ∥; T029 requires hardware.

### Within each user story

- Tests are written alongside the code they cover and must pass before the story's validate task.
- Shared state / driver before consumers; consumers before emulator parity; emulator parity before emulator tests.

### Parallel opportunities

- **Phase 2**: T004 ∥ T005 once T003 lands.
- **US1**: firmware chain (T006 → …) ∥ web chain (T009 → T010) ∥ i18n (T011) ∥ emulator chain (T012 → T013).
- **US2**: T016 ∥ T017 after T015; T019 independent of the firmware history work.
- **Cross-story**: US2 (history) and US3 (screen) can proceed in parallel with different developers once US1's T007 and T015's file are not in flight.
- **Merge hotspots — keep sequential within each file**: `components/web_server/handlers_mgmt.c` (T006, T017); `components/web_server/www/mgmt/mgmt.js` (T010, T018) and `mgmt.html` (T009, T018); `components/display/ui.c` (T007, T022); `tools/hw_emulator.py` (T012, T019, T024); `tools/test_hw_emulator.py` (T013, T020, T025).

---

## Parallel Example: User Story 1

```bash
# After T002 (app_ctx) is done, three independent chains:
# Chain A (firmware):  T006 (api/status) then T007 (ui.c) then T008 (display.c)
# Chain B (web):       T009 (mgmt.html)  then T010 (mgmt.js)      ∥  T011 (i18n packs)
# Chain C (emulator):  T012 (hw_emulator.py) then T013 (test_hw_emulator.py)
# Converge at T014 (validate US1)
```

---

## Implementation Strategy

### MVP first (User Story 1 only)

1. Phase 1 Setup → Phase 2 Foundational (driver + state + detection).
2. Phase 3 US1 → live humidity on screen and web, emulator-verified (T014).
3. **STOP and VALIDATE** against quickstart §2.1–2.2; demo.

### Incremental delivery

1. Setup + Foundational → foundation ready (BME280 read, feature-005 fittings unchanged).
2. + US1 → live humidity (MVP).
3. + US2 → humidity history (JSON/CSV/table), pre-007 data intact.
4. + US3 → the four-quadrant screen validated across all fittings.
5. + US4 → autodetect ladder + non-BME280 regression proof.
6. Polish → full gate + README + on-device pass (T027–T029).

Each checkpoint is independently shippable; on-device steps (T029) close SC-001/002/005/006/007 on real hardware.
