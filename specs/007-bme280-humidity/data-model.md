# Data Model: BME280 Humidity Support & Quadrant Main Screen

**Feature**: 007-bme280-humidity | **Date**: 2026-08-29

Extends feature 005's data model. Only additions/changes are shown.

## Entity: Humidity reading (shared state)

```c
typedef struct {
    float    value_pct;     /* relative humidity, 0.0–100.0 %RH */
    bool     valid;         /* false: no BME280, read error, or out of range */
    int64_t  updated_at_ms; /* esp_timer_get_time()/1000 at last write */
} humidity_reading_t;
```

Lives in `app_state.humidity`, guarded by `app_state_mutex`, written only by the sensor task, read by display / web / history. Same lifecycle as `temperature_reading_t` / `pressure_reading_t`.

**Validation**: plausible range 0.0–100.0 %RH; outside → `valid = false` (FR-007). A sustained 100.0 %RH (saturation) is a valid measurement, not an error. Temperature, pressure and humidity from one BME280 cycle come from a single 8-byte burst read and share `t_fine`, so they are mutually consistent.

## Entity: Sensor configuration (boot-time detection result)

```c
typedef enum {
    SENSOR_NONE = 0,  /* no sensor: all readings invalid            */
    SENSOR_DS18B20,   /* wired probe: temperature only              */
    SENSOR_BMP280,    /* I2C BMP280: temperature + pressure         */
    SENSOR_BME280,    /* I2C BME280: temperature + pressure + humidity */
} sensor_kind_t;
```

Stored in `app_state.sensor_kind`; fixed from `sensor_start()` until reboot.

| Detected | Temperature | Pressure | Humidity | Notes |
|----------|-------------|----------|----------|-------|
| `SENSOR_BME280` | BME280 | BME280 | BME280 | chip-id `0x60`; lower rungs ignored |
| `SENSOR_BMP280` | BMP280 | BMP280 | never valid | chip-id `0x58`; feature-005 behaviour |
| `SENSOR_DS18B20` | probe | never valid | never valid | pre-005 behaviour |
| `SENSOR_NONE` | invalid | invalid | invalid | device otherwise fully functional |

**State transitions**: none at runtime. Read failures flip reading validity, never `sensor_kind`. Reboot re-runs detection (0x76 → 0x77; chip-id `0x60` wins a tie over `0x58`).

## Entity: History record v2 (on-disk) — reserved bytes spent

The feature-005 v2 record is **unchanged in size (12 B), file name (`YYYYMM.bi2`) and CRC coverage (bytes [0..10])**. The `reserved[2]` field is now `hum_centi`, and `flags` gains bit2.

```c
typedef struct __attribute__((packed)) {
    uint32_t epoch;        /* off 0  UTC seconds                              */
    int16_t  temp_centi;   /* off 4  °C × 100                                 */
    uint16_t press_deci;   /* off 6  hPa × 10;  meaningful iff flags bit1     */
    uint8_t  flags;        /* off 8  bit0 temp valid | bit1 pressure recorded | bit2 humidity recorded */
    uint16_t hum_centi;    /* off 9  %RH × 100; meaningful iff flags bit2  (was reserved[2]) */
    uint8_t  crc8;         /* off 11 CRC-8 poly 0x07 over bytes [0..10]       */
} hist_record_v2_t;        /* sizeof == 12 (static-asserted, unchanged)       */
```

**Byte layout is unchanged from feature 005** — `hum_centi` occupies exactly the two bytes `reserved[2]` did (offsets 9–10); `flags` stays at offset 8. This is what keeps existing `.bi2` records readable: an old record has those two bytes zero, so `flags bit2 == 0` ⇒ humidity absent. Do **not** move `hum_centi` ahead of `flags`.

| Aspect | before 007 | after 007 |
|--------|-----------|-----------|
| Size / file name / CRC | 12 B / `.bi2` / [0..10] | **identical** |
| `reserved[2]` | zero-filled, ignored | `hum_centi` |
| `flags` bits used | 0, 1 | 0, 1, **2** |
| Reading an old `.bi2` record | — | bit2 clear ⇒ humidity **absent** (`NAN`) |
| Reading a `.bin` (v1) record | pressure absent | pressure **and** humidity absent |

Encoding: `hum_centi = round(pct × 100)` after the 0–100 range check; set flags bit2. Decoding: `flags & bit2 ? hum_centi / 100.0f : NAN`. Never emit `0` for "not recorded" (FR-006).

**Query callback change** (`history.h`):

```c
typedef void (*history_cb_t)(uint32_t epoch, float temp_c,
                             float pressure_hpa, float humidity_pct, void *ctx);
/* pressure_hpa / humidity_pct are NAN when not recorded */
```

All three callers updated in this feature: JSON stream, CSV stream, `test_history.c`.

## Entity: Main screen layout (LVGL widgets)

Not persisted — describes the `display` component's widget tree after `ui_init()`. Full geometry in [contracts/screen-layout.md](./contracts/screen-layout.md).

- Four quadrant containers, one per screen corner, fixed positions independent of `sensor_kind`.
- Each quadrant: a value label + a sub-label (unit or mode badge).
- One centred WiFi glyph label.
- Unavailable readings → dashed placeholder text in the value label; the quadrant is never hidden or emptied.

## `/api/status` additions (details in [contracts/readings-api.md](./contracts/readings-api.md))

```json
{
  "pressure_hpa": 1013.2,
  "pressure_valid": true,
  "humidity_pct": 47.5,
  "humidity_valid": true,
  "sensor": "bme280"
}
```

- `humidity_valid` is `true` only when `sensor == "bme280"` and the latest reading is in range.
- `pressure_valid` is `true` for `sensor` `"bmp280"` or `"bme280"`.
- History JSON record: `{"timestamp":…, "temperature":…, "pressure": <n|null>, "humidity": <n|null>}`.
- CSV header: `timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct` (humidity cell empty when absent).

## i18n additions (all four packs; enforced by `tools/check_i18n.py`)

| Key | en value |
|-----|----------|
| `mgmt_label_humidity` | `Humidity` |
| `mgmt_th_humidity` | `Humidity` |

Units `%` / `%RH` and `hPa` are fixed markup (not translated). Additional keys may be added during implementation if new visible status text appears; the checker keeps the four packs in lockstep (61 → 63 keys × 4).
