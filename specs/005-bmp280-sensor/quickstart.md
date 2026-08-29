# Quickstart: Validating BMP280 Temperature/Pressure Support

**Feature**: 005-bmp280-sensor | **Date**: 2026-07-14

Contract details: [contracts/readings-api.md](./contracts/readings-api.md); record/state model: [data-model.md](./data-model.md).

## Prerequisites

- Python 3.11+ (emulator checks); ESP-IDF v6.0.x for firmware checks
- Hardware for §3: ESP32 board, BMP280 breakout (I2C), DS18B20 probe
  - Wiring: BMP280 **SCL → GPIO22**, **SDA → GPIO21**, VCC → 3V3, GND → GND (SDO floating/low = address 0x76, high = 0x77 — both work)
  - DS18B20 stays on GPIO27 as today

## 1. Automated checks (no hardware)

```powershell
python tools/test_hw_emulator.py        # unit + HTTP contract tests incl. pressure & fittings
python tools/check_i18n.py              # new pressure labels present in all 4 packs
idf.py build                            # firmware compiles
```

**Expected**: all pass; checker reports 0 discrepancies.

On-target unit tests (component test app, run with any ESP32 attached): BMP280 compensation math reproduces the datasheet example (25.08 °C / 1006.53 hPa), history v2 records round-trip with CRC, v1 records still decode.

## 2. Browser walk against the emulator

```powershell
python tools/hw_emulator.py
# management page: http://127.0.0.1:8080/   scenario control: http://127.0.0.1:8080/emu
```

| Step | Scenario (`/emu`) | Expected on management page |
|------|-------------------|------------------------------|
| 2.1 | default (`sensor: bmp280`) | Current Readings shows a Pressure row ≈ 1005–1021 hPa, refreshing with temperature |
| 2.2 | `sensor: bmp280`, `sensor_valid: false` | Temperature `---` AND pressure `---` (BMP280 supplies both) |
| 2.3 | `sensor: ds18b20` | Pressure row hidden; temperature works; zero pressure artifacts anywhere (SC-003) |
| 2.4 | `sensor: none` | Temperature `---`; pressure hidden; page otherwise fully functional |
| 2.5 | back to `bmp280`; load history | History table shows a pressure column; records from step 2.3's window show blank pressure |
| 2.6 | Download CSV | Header `timestamp_iso8601,temperature_c,pressure_hpa`; blanks for the ds18b20 window |
| 2.7 | German browser language | Pressure label appears in German (feature-004 mechanism) |

## 3. On-device validation

```powershell
idf.py build flash monitor
```

1. **BMP280 boot (US1)**: with BMP280 wired, boot. Log shows BMP280 detected (address noted). Within 10 s the LCD shows temperature AND pressure with "hPa"; `curl -k https://weather-<suffix>.local/api/status` reports `"sensor":"bmp280"`, `"pressure_valid":true`, plausible `pressure_hpa` (SC-001). Compare against a local reference/METAR: within ±3 hPa (SC-002 — note: station pressure, not sea-level; at altitude compare with an absolute reference).
2. **Warm test**: hold a finger on the BMP280 — displayed temperature rises (proves source is the BMP280, not the probe, even with both wired).
3. **Probe-only boot (US3/SC-003)**: disconnect BMP280, reboot. Log shows DS18B20 fallback; LCD/page identical to pre-feature behavior; status reports `"sensor":"ds18b20"`, `"pressure_valid":false`.
4. **No-sensor boot**: disconnect both, reboot → readings unavailable, web/WiFi/time all functional.
5. **Mid-run failure (SC-006)**: in BMP280 mode, pull SDA. Within 15 s both readings show unavailable; no bogus history samples (check the next 5-min window in `/api/history`). Reconnect SDA → readings resume within a few cycles (same-sensor recovery, no reboot).
6. **Plug-and-reboot upgrade (SC-005)**: starting from probe-only, wire the BMP280 and power-cycle → pressure appears with zero configuration.
7. **History over time (US2/SC-004)**: run ≥1 h in BMP280 mode; `/api/history` shows pressure on new samples; CSV reproduces them; records from before the upgrade show `null`/blank pressure.

## Pass criteria summary

| Success criterion | Proven by |
|-------------------|-----------|
| SC-001 pressure ≤10 s from power-on | §3.1 |
| SC-002 ±3 hPa vs reference, tracks change | §3.1, §3.2 |
| SC-003 probe-only zero regression | §2.3, §3.3 |
| SC-004 ≥95% of 5-min samples carry pressure over 24 h | §3.7 (long-run), §2.5 |
| SC-005 plug-and-reboot upgrade | §3.6 |
| SC-006 unavailable ≤15 s on failure, no bogus history | §3.5 |
