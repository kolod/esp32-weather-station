# Research: BME280 Humidity Support & Quadrant Main Screen

**Feature**: 007-bme280-humidity | **Date**: 2026-08-29

Builds on feature 005 research (D1–D8). Only the deltas are recorded here.

## D1: BME280 support — extend the in-house driver vs new driver vs managed component

**Decision**: Extend the existing in-house driver `components/sensor/bmp280.{c,h}` to also recognise and read a BME280. Keep the file and symbol prefix `bmp280_*`; add BME280-only pieces (`bme280_compensate_h`, humidity calibration fields, `ctrl_hum`).

**Rationale**: The BME280 is a superset of the BMP280 on the same bus and address range. Per the Bosch datasheets (BST-BMP280-DS001 §3.11.3 and BST-BME280-DS002 §4.2.3) the **temperature and pressure compensation formulas are byte-for-byte identical** and `t_fine` is shared. The BME280 delta is: chip-id `0x60` instead of `0x58` at register `0xD0`; an extra calibration block (`dig_H1` at `0xA1`; `dig_H2..dig_H6` packed across `0xE1..0xE7`); a `ctrl_hum` register (`0xF2`) that must be written **before** `ctrl_meas` to take effect; the measurement burst grows from 6 to 8 bytes (`0xF7..0xFE`, humidity is the trailing `hum_msb, hum_lsb`); and one humidity compensation function. Forking would duplicate ~90 lines of identical compensation and calibration-parse code and a second CMake/test target for no isolation benefit. A managed component was already rejected in feature 005 D1 (legacy `driver/i2c.h` API, IDF 6.0 uncertainty) — nothing changed.

**Alternatives considered**:
- New `bme280.c` sharing `bmp280_compensate_t/p` via the header — still needs its own detect/configure/read/calib-parse; ~2× surface for one extra formula.
- `espressif/bmp280` + `boschsensortec/BME280_SensorAPI` managed components — same supply-chain and API-generation concerns as 005 D1.

## D2: Detection order and the BME280 > BMP280 > DS18B20 > none ladder

**Decision**: In `bmp280_detect()`, probe 0x76 then 0x77 as today; read chip-id at each ACKing address. Accept `0x58` (BMP280) **or** `0x60` (BME280); record `has_humidity = (chip_id == 0x60)` in the handle. If **both** addresses hold Bosch sensors, prefer the one reporting `0x60`. `sensor.c` then maps: `has_humidity` → `SENSOR_BME280`; else Bosch found → `SENSOR_BMP280`; else DS18B20 probe → `SENSOR_DS18B20`; else `SENSOR_NONE`.

**Rationale**: A single sensor occupies one address, so in the common case the "ladder" is just "which chip-id did the one Bosch sensor return". The dual-sensor tie-break (prefer humidity-capable) satisfies the spec edge case "richest source wins" at the cost of one extra probe. Detection remains startup-only and fixed until reboot (feature 005 D6); runtime read failures invalidate readings and retry the same sensor, never switch rungs.

**Alternatives considered**: separate BME280 I2C scan pass (redundant — same addresses); a Kconfig switch to force sensor kind (contradicts the "no configuration, autodetect" requirement).

## D3: Humidity in history — reuse the v2 record, no version bump

**Decision**: Keep the feature-005 v2 record at **12 bytes**, file extension **`.bi2`**, CRC over bytes `[0..10]` — all unchanged. Reinterpret the existing `uint8_t reserved[2]` as `uint16_t hum_centi` (%RH × 100, 0–10000). Add `flags` **bit2** = "humidity recorded". Encode only when the humidity reading is valid and in 0–100 %RH; decode to `NAN` whenever bit2 is clear (covers every pre-007 v2 record, where `reserved` is zero, and all v1 records).

**Rationale**: Feature 005 D4 explicitly sized the reserved bytes "for one future field (e.g. humidity) without another version bump" — this is that field. No new file extension means no new dispatch branch in the reader, no purge change, no migration. `hum_centi` gives 0.01 %RH resolution, far finer than the sensor's ±3 %RH accuracy. Backward compatibility is automatic: old records have `flags bit2 == 0`.

**Alternatives considered**:
- v3 record / `.bi3` files — a third size to dispatch and sort, for 2 bytes that already exist.
- Store humidity in a sidecar file — splits a sample across two files, complicates retention and torn-write handling.
- `hum_deci` in one byte (0–1000 → doesn't fit `uint8`; 0–100 whole-percent in one byte, losing the decimal) — wastes the second reserved byte and drops resolution for no gain.

## D4: `/api/status` and history payload shape

**Decision**:
- `/api/status` gains `"humidity_pct": <float, 1 decimal>` and `"humidity_valid": <bool>`; the `sensor` string gains the value `"bme280"`. Stack buffer `char buf[1024]` → `char buf[1152]`.
- History JSON records gain `"humidity": <float|null>` (null = not recorded), mirroring `"pressure"`.
- CSV gains a 4th column; header becomes `timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct`; empty cell when absent.
- Pressure is now reported valid for `sensor` `"bmp280"` **and** `"bme280"`.

**Rationale**: Purely additive — every existing key/column keeps its position and meaning, so feature-005-era clients and emulator tests keep working (SC-003). `null`/empty for absent humidity, never `0` (FR-006). "%RH"/"%" and "hPa" remain untranslated units.

**Alternatives considered**: a nested `"readings": {…}` object (breaks existing flat-key consumers); omitting the humidity keys entirely on non-BME280 devices (feature 005 already established "present but false" for pressure — stay consistent).

## D5: LCD four-quadrant layout

**Decision**: Rebuild `ui_init()` for the 250×135 landscape ST7789 as four quadrants plus a centred WiFi glyph:

| Quadrant | Region (x, y, w, h) | Content | Value font | Sub-label font |
|----------|--------------------|---------|-----------|----------------|
| Top-left | 0, 0, 125, 67 | `HH:MM` + `LOCAL`/`UTC` | Montserrat 36 | Montserrat 14 |
| Top-right | 125, 0, 125, 67 | temperature + `°C`/`°F` | Montserrat 36 | Montserrat 14 |
| Bottom-left | 0, 68, 125, 67 | pressure + `hPa` | Montserrat 20 | Montserrat 14 |
| Bottom-right | 125, 68, 125, 67 | humidity + `%` | Montserrat 20 | Montserrat 14 |
| Centre | screen mid | `LV_SYMBOL_WIFI` | (default) | — |

Each quadrant is an `lv_obj` container aligned to a screen corner; value + sub-label stacked inside. Unavailable readings render `--:--`, `---`, `--- hPa`, `--- %` in place (FR-012). The WiFi glyph sits at `LV_ALIGN_CENTER`, colour = state (connected → light-blue, else grey), as today.

**Rationale**: Per user preference the screen uses **exactly two faces** — one for every value, one for every label — rather than a per-quadrant size mix. `sdkconfig.defaults` is changed to enable `CONFIG_LV_FONT_MONTSERRAT_28` (values) and drop the now-unused 20/36/48; Montserrat 14 (the LVGL default) covers all labels. `ui.c` references both through `FONT_VALUE` / `FONT_LABEL` macros. At 28 px a 6-glyph value (`1013.2`) is ≈85 px in the 125 px column — comfortably clear of the centre glyph, so no per-value abbreviation is needed. Verified with `idf.py build` (clean). Constitution III is satisfied: the `sdkconfig.defaults` change carries a verified rebuild.

**Alternatives considered**: a 2×2 `lv_table`/grid layout (heavier, harder to style per-cell); keeping the WiFi glyph in the corner (spec explicitly says centre); showing pressure to 0 decimals to keep font 36 (loses the on-screen precision the sensor supports — history already keeps 0.1).

**Supersedes**: feature 005's `ui_set_pressure(..., bool present)` hide-when-absent behaviour. The `present` parameter is removed; the pressure quadrant is always shown.

## D6: Humidity compensation test vector

**Decision**: Port the Bosch **32-bit fixed-point** humidity compensation (`bme280_compensate_H`, BST-BME280-DS002 §4.2.3, `BME280_compensate_H_int32`). The BME280 datasheet gives no single numeric worked example for humidity (unlike BMP280 T/P). Test by **cross-checking the fixed-point result against the datasheet's double-precision reference formula** (`bme280_compensate_H_double`) for a fixed calibration set and several raw humidity codes spanning 0 %RH → clamp-at-100 %RH, asserting agreement within 1 %RH, plus the explicit clamp behaviour at both ends.

**Rationale**: The float reference formula is unambiguous and easy to evaluate off-target; requiring the two independent implementations to agree catches transcription errors in the bit-shift version — the same defence the BMP280 tests get from the datasheet example. Clamp cases (`< 0 → 0`, `> 100 → 100`, i.e. `> 419430400 >> 12`) are asserted directly.

**Alternatives considered**: trusting the fixed-point port unverified (the exact class of bug feature 005's Unity tests exist to catch); pulling a third-party test vector of uncertain provenance.

## D7: Emulator & test strategy

**Decision**:
- `hw_emulator.py`: `SENSOR_KINDS` becomes `("bme280", "bmp280", "ds18b20", "none")`, default stays `"bmp280"` (keeps existing test expectations stable; `bme280` is opt-in via `--sensor` / `/emu`). `humidity_at(epoch)` — deterministic sinusoid ~50 ± 20 %RH over ~1 day + small per-minute noise, clamped 0–100, same shape as `temperature_at`/`pressure_at`. `status()` adds `humidity_pct` / `humidity_valid` (`sensor == "bme280"`); `pressure_valid` widens to `sensor in ("bmp280", "bme280")`. History JSON/CSV gain humidity from `history_humidity_at(ts)` (value when the fitting at `ts` was `bme280`, else `None`). `BOOTLOG_SENSOR_LINES` gains a `"bme280"` entry ("BME280 found at 0x76" / "Sensor mode: BME280 (temperature + pressure + humidity)").
- Unity: `test_bmp280.c` gains the D6 humidity cases; `test_history.c` gains v2 humidity encode/decode/CRC and "legacy v2 + v1 decode with humidity absent".
- Emulator suite (`test_hw_emulator.py`): status shape for the `bme280` fitting; history JSON `humidity` null-vs-value across a fitting switch; CSV 4-column header + blanks; regression that `bmp280`/`ds18b20`/`none` never emit a humidity *value* (SC-003).
- On-device quickstart: BME280 boot (all four quadrants populated), BMP280 boot (humidity quadrant = `--- %`), probe-only boot (pressure + humidity both placeholder), mid-run disconnect (all readings unavailable ≤15 s), plug-and-reboot upgrade.

**Rationale**: Mirrors the feature 003/004/005 pattern exactly — pure functions (compensation, codec) covered on-target by Unity; everything browser-visible covered by the emulator suite; the physical screen and real sensor covered by the quickstart.

## Resolved unknowns

No `NEEDS CLARIFICATION` markers were in the spec. Open implementation choices (font sizing fallback D5, humidity test-vector method D6) have documented decisions with fallbacks; both are verifiable during implementation without changing scope.
