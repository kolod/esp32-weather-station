# Research: BMP280 Temperature/Pressure Sensor

**Feature**: 005-bmp280-sensor | **Date**: 2026-07-14

## D1: BMP280 driver — managed component vs in-house

**Decision**: Implement a minimal in-house driver (`components/sensor/bmp280.c`, ~150 lines) against ESP-IDF v6's `i2c_master` API.

**Rationale**: The BMP280 protocol is small and fully documented (Bosch datasheet BST-BMP280-DS001): read chip-id register `0xD0` (expect `0x58`), read 24 bytes of factory calibration (`0x88–0x9F`), write `ctrl_meas`/`config`, burst-read 6 measurement bytes (`0xF7–0xFC`), apply the datasheet's integer compensation formulas. The compensation math is pure logic with a **worked example in the datasheet §3.11.3** (raw T=519888, P=415148 → 25.08 °C, 1006.5325 hPa), giving an exact Unity test vector. Registry components for BMP280 either target the legacy `driver/i2c.h` API (deprecated in IDF 6), pull in wider Bosch abstraction layers, or have uncertain IDF 6.0 compatibility — an external dependency saves nothing here and adds supply-chain surface.

**Alternatives considered**: `espressif/bme280`-family managed components (API-generation mismatch risk on IDF v6, larger than needed); Bosch's official BMP2 sensor API (portable C, but needs an I2C glue layer anyway — the glue *is* most of an in-house driver).

## D2: I2C bus configuration

**Decision**: One `i2c_master` bus on **SCL=GPIO22, SDA=GPIO21** (user-specified), 400 kHz, internal pull-ups enabled, created by the sensor component at startup. Probe addresses **0x76 then 0x77** (SDO strap selects between them on breakout boards); first address whose chip-id reads `0x58` wins.

**Rationale**: GPIO21/22 are the ESP32's conventional I2C pins and are unused (DS18B20 owns GPIO27, display is SPI, buttons elsewhere). 400 kHz is the BMP280's standard fast-mode rate. Internal pull-ups make bare-module wiring work; boards with external pull-ups are unaffected. Probing both addresses makes any common breakout work unmodified.

**Alternatives considered**: 100 kHz (no benefit, slower); fixed 0x76 only (fails on SDO-high boards for zero savings).

## D3: Measurement mode and cadence

**Decision**: Normal mode with the datasheet's "weather monitoring" oversampling recommendation adjusted for a 5 s read cycle: temperature ×2, pressure ×16, IIR filter coefficient 4, standby 1000 ms. The sensor task reads the latest measurement every 5 s (existing cadence) via one 6-byte burst read.

**Rationale**: ×16 pressure oversampling + IIR gives low-noise readings suitable for trend display (spec SC-002: ±3 hPa vs reference); normal mode avoids per-cycle forced-mode handshakes; one burst read guarantees temperature/pressure consistency for the compensation formula (t_fine coupling). Power is irrelevant (mains-powered device).

**Alternatives considered**: Forced mode per 5 s cycle (more protocol steps, no benefit on mains power); handheld-device settings (higher data rate than needed).

## D4: History record format versioning

**Decision**: New **v2 record, 12 bytes packed**: `epoch u32, temp_centi i16, press_deci u16 (hPa × 10), flags u8 (bit0 temp valid, bit1 pressure valid/recorded), reserved u8, crc8 u8` — CRC over bytes 0..10. New monthly files are written with extension **`.bi2`** (`YYYYMM.bi2`); existing `.bin` files (8-byte v1 records) remain read-only history. The reader dispatches record size by file extension; purge handles both.

**Rationale**: Pressure 300.0–1100.0 hPa × 10 fits u16 with 0.1 hPa resolution (matches sensor's practical accuracy). Distinguishing versions by filename avoids in-band format sniffing on size-8-vs-12 records and needs no migration pass — old data ages out through the normal 3-month purge (FR-006: old records stay readable, pressure clearly absent). The reserved byte keeps the record at a round 12 bytes with room for one future field (e.g., humidity) without another version bump.

**Alternatives considered**: Rewriting v1 files to v2 on first boot (a migration burst of flash writes for data that self-expires in ≤3 months — rejected); per-file header magic (more code than extension dispatch, same result); keeping 8 bytes by dropping CRC (torn-record detection has already proven useful).

## D5: Shared state and events

**Decision**: Add `pressure_reading_t { float value_hpa; bool valid; int64_t updated_at_ms; }` and `app_state.pressure` alongside the existing `reading`. The sensor task updates both under the existing mutex and posts the existing `APP_EVT_READING_UPDATED` once per cycle. A `sensor_kind_t { SENSOR_NONE, SENSOR_DS18B20, SENSOR_BMP280 }` records the boot-time detection result in `app_state` for the status API and display.

**Rationale**: One event per cycle keeps history sampling logic unchanged (it already reads `app_state` on each event); consumers that don't care about pressure are untouched. Exposing the detected sensor makes FR-002 observable and testable (`/api/status` reports it; quickstart asserts it).

**Alternatives considered**: Separate `APP_EVT_PRESSURE_UPDATED` (forces every consumer to handle two events for one physical read — needless); packing pressure into `temperature_reading_t` (muddles a type consumed by temperature-only code paths).

## D6: Detection & failure semantics (spec FR-002, edge cases)

**Decision**: At `sensor_start`: probe I2C for BMP280 (D2); if found, mode = BMP280 and the DS18B20/1-Wire bus is never initialized. If not found, initialize DS18B20 exactly as today (including its existing per-cycle reconnect retry). In BMP280 mode, a failed read marks **both** readings invalid; the task re-runs BMP280 init on subsequent cycles (same-sensor recovery, mirroring the probe's existing behavior) but never falls back to the probe mid-run — a reboot re-detects.

**Rationale**: Matches the user's explicit "detect on startup" answer and the spec's no-silent-source-switch edge case, while keeping the recovery-after-reconnect behavior users already get from the probe path. Skipping 1-Wire init in BMP280 mode avoids wasting the RMT channel and boot time.

## D7: Surfacing pressure (API, UI, display, i18n)

**Decision**:
- `/api/status` gains `"pressure_hpa": <float>`, `"pressure_valid": <bool>`, and `"sensor": "bmp280"|"ds18b20"|"none"`; the JSON buffer grows 896 → 1024 B.
- History JSON records gain `"pressure": <float|null>` (null = not recorded); CSV gains a third column `pressure_hpa` (empty for v1/absent samples) — header becomes `timestamp_iso8601,temperature_c,pressure_hpa`.
- Management page: pressure row in Current Readings (`data-i18n="mgmt_label_pressure"`, value + fixed "hPa" unit), rendered like temperature's valid/`---` pattern; history table gains a pressure column shown only when data contains pressure.
- LCD: pressure line ("1013.2 hPa") under the temperature area using an existing mid-size font; hidden/`---` when unavailable. Layout verified in quickstart.
- New i18n keys (`mgmt_label_pressure`, `mgmt_th_pressure`, plus any status text) added to all four packs; `check_i18n.py` enforces completeness automatically.

**Rationale**: Additive JSON fields keep old emulator tests/clients working (SC-003: probe-only stations simply report `pressure_valid:false` / `sensor:"ds18b20"`... see contract note on omission vs false); "hPa" is not translated (SI unit). CSV column order appends to avoid breaking column-index consumers of the existing two columns.

## D8: Emulator & test strategy

**Decision**:
- `hw_emulator.py`: `pressure_at(epoch)` — slow sinusoid 1013 ± 8 hPa over ~2 days + small noise, deterministic like `temperature_at`; scenario gains `"sensor": "bmp280"|"ds18b20"|"none"` (default `bmp280`) driving which status/history fields are populated; history/CSV emit pressure only in `bmp280` mode.
- Unity: `test_bmp280.c` runs the datasheet compensation example (exact expected values) plus range-clamp cases; `test_history.c` gains v2 encode/decode/CRC and a v1-still-decodes case.
- Emulator suite: status shape per fitting; history JSON `pressure` null-vs-value; CSV three-column header and blanks; probe-only regression (no pressure artifacts anywhere — SC-003).
- On-device: quickstart covers real BMP280 boot, probe-only boot, no-sensor boot, mid-run disconnect (SC-006), and plug-and-reboot upgrade (SC-005).

**Rationale**: The compensation math and record codec are the two pieces most likely to be subtly wrong and both are pure functions — exactly what Unity on-target tests do well. Everything browser-visible is already covered by the emulator pattern established in features 003/004.

## Resolved unknowns

No NEEDS CLARIFICATION items remained: pins were supplied by the user (D2), sensor-role and history questions were resolved during specification, and all other choices had clear defaults documented above.
