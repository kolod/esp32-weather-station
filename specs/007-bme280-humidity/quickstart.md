# Quickstart: Validating BME280 Humidity & the Quadrant Screen

**Feature**: 007-bme280-humidity | **Date**: 2026-08-29

Contract details: [contracts/readings-api.md](./contracts/readings-api.md), [contracts/screen-layout.md](./contracts/screen-layout.md); state model: [data-model.md](./data-model.md).

## Prerequisites

- Python 3.11+ (emulator checks); ESP-IDF v6.0.2 for firmware checks
- Hardware for §3: ESP32 board, **BME280** breakout (I2C), plus a BMP280 breakout and a DS18B20 probe to exercise the fallback rungs
  - Wiring is identical to feature 005: **SCL → GPIO22**, **SDA → GPIO21**, VCC → 3V3, GND → GND (SDO low = 0x76, high = 0x77 — both work). DS18B20 stays on GPIO27.
- A reference hygrometer for §3 (SC-002)

## 1. Automated checks (no hardware)

```powershell
python tools/test_hw_emulator.py        # unit + HTTP contract tests incl. humidity & the bme280 fitting
python tools/check_i18n.py              # mgmt_label_humidity / mgmt_th_humidity in all 4 packs
idf.py build                            # firmware compiles, zero warnings
```

**Expected**: all pass; checker reports 0 discrepancies (63 keys × 4 languages).

On-target unit tests (`idf.py -C components/sensor/test build`, then flash to any ESP32):
- BME280 humidity compensation: fixed-point port agrees with the datasheet double formula within 1 %RH across the range; clamps at 0 and 100 %RH (research D6).
- `test_history.c`: v2 records round-trip humidity with CRC; legacy v2 records (reserved bytes zero) and v1 records still decode with humidity absent.

## 2. Browser walk against the emulator

```powershell
python tools/hw_emulator.py --sensor bme280
# management page: http://127.0.0.1:8080/   scenario control: http://127.0.0.1:8080/emu
```

| Step | Scenario (`/emu`) | Expected on management page |
|------|-------------------|------------------------------|
| 2.1 | `sensor: bme280` | Current Readings shows Pressure **and** Humidity rows; humidity ≈ 30–70 %RH, refreshing with temperature |
| 2.2 | `sensor: bme280`, `sensor_valid: false` | Temperature `---`, pressure `---`, humidity `---` (one sensor supplies all three) |
| 2.3 | `sensor: bmp280` | Pressure row shown; **humidity row hidden**; no humidity artefacts anywhere (SC-003) |
| 2.4 | `sensor: ds18b20` | Pressure and humidity rows both hidden; temperature works |
| 2.5 | `sensor: none` | Temperature `---`; pressure + humidity hidden; page otherwise fully functional |
| 2.6 | back to `bme280`; Load last 100 records | History table shows Humidity column; records from the step 2.3/2.4 window show blank humidity |
| 2.7 | Download CSV | Header `timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct`; blanks for the non-BME280 window |
| 2.8 | German / French / Ukrainian browser language | "Humidity" label appears translated (feature-004 mechanism), English fallback if a pack is missing it |

## 3. On-device validation

```powershell
idf.py build flash monitor
```

1. **BME280 boot (US1 / SC-001)**: with the BME280 wired, boot. Log shows `Sensor mode: BME280 (temperature + pressure + humidity)`. Within 10 s the LCD's four quadrants show time (TL), temperature (TR), pressure (BL), humidity (BR) with the WiFi glyph centred. `curl -k https://weather-<suffix>.local/api/status` reports `"sensor":"bme280"`, `"humidity_valid":true`, plausible `humidity_pct`.
2. **Humidity tracks reality (SC-002)**: breathe near the sensor — humidity rises within ~15 s, then settles. Compare the steady reading against a reference hygrometer: within ±5 %RH.
3. **Screen layout (US3)**: confirm the four corners are in the positions above and do not overlap the centre glyph; press the right button (°C↔°F) — only the temperature quadrant changes; press the left button (LOCAL↔UTC) — only the time quadrant changes.
4. **BMP280 boot**: swap the BME280 for a BMP280, reboot. Log shows `Sensor mode: BMP280 (temperature + pressure)`. LCD humidity quadrant shows `--- %`; all other quadrants populated. `/api/status`: `"sensor":"bmp280"`, `"humidity_valid":false`, `"pressure_valid":true`.
5. **Probe-only boot (SC-003)**: swap in the DS18B20 only, reboot. LCD shows `--- hPa` and `--- %` placeholders; time and temperature work. `/api/status`: `"sensor":"ds18b20"`, both `pressure_valid` and `humidity_valid` false; no humidity value on any surface.
6. **No-sensor boot**: disconnect all sensors, reboot → all four quadrants show placeholders; web / WiFi / time all functional.
7. **Mid-run failure (SC-006)**: in BME280 mode, pull SDA. Within 15 s temperature, pressure and humidity all show unavailable; the next 5-minute `/api/history` sample carries no humidity value. Reconnect SDA → readings resume within a few cycles, no reboot.
8. **Plug-and-reboot upgrade (SC-005)**: from BMP280, wire a BME280 and power-cycle → humidity appears with zero configuration; swap back and power-cycle → returns to BMP280 behaviour.
9. **History over time (US2 / SC-004)**: run ≥1 h in BME280 mode; `/api/history` shows humidity on new samples; CSV reproduces them; records from before the upgrade show `null` / blank humidity.

## Pass criteria summary

| Success criterion | Proven by |
|-------------------|-----------|
| SC-001 humidity ≤10 s from power-on | §3.1 |
| SC-002 ±5 %RH vs reference, tracks change ≤15 s | §3.2 |
| SC-003 non-BME280 zero regression / no artefacts | §2.3–2.5, §3.4, §3.5 |
| SC-004 ≥95% of 5-min samples carry humidity over 24 h | §3.9 (long-run), §2.6 |
| SC-005 plug-and-reboot upgrade both directions | §3.8 |
| SC-006 unavailable ≤15 s on failure, no bogus history | §3.7 |
| SC-007 four corners identical across stations, readable in 5 s | §3.3–3.6 |
| SC-008 humidity label in all 4 languages with English fallback | §2.8 |
