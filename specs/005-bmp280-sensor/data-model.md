# Data Model: BMP280 Temperature/Pressure Sensor

**Feature**: 005-bmp280-sensor | **Date**: 2026-07-14

## Entity: Pressure reading (shared state)

```c
typedef struct {
    float    value_hpa;     /* station pressure in hPa */
    bool     valid;         /* false: no BMP280, read error, or out of range */
    int64_t  updated_at_ms; /* esp_timer_get_time()/1000 at last write */
} pressure_reading_t;
```

Lives in `app_state.pressure`, guarded by `app_state_mutex`, written only by the sensor task, read by display / web / history. Same lifecycle rules as the existing `temperature_reading_t`.

**Validation**: plausible range 300.0–1100.0 hPa (datasheet operating range); values outside → `valid = false` (FR-007). Both readings from one BMP280 cycle share one burst read, so they are mutually consistent.

## Entity: Sensor configuration (boot-time detection result)

```c
typedef enum { SENSOR_NONE = 0, SENSOR_DS18B20, SENSOR_BMP280 } sensor_kind_t;
```

Stored in `app_state.sensor_kind`; fixed from `sensor_start()` until reboot (research D6).

| Detected | Temperature source | Pressure | Notes |
|----------|-------------------|----------|-------|
| `SENSOR_BMP280` | BMP280 | BMP280 | Probe ignored even if attached |
| `SENSOR_DS18B20` | DS18B20 probe | never valid | Behavior identical to pre-feature firmware |
| `SENSOR_NONE` | none (invalid) | never valid | Device otherwise fully functional |

**State transitions**: none at runtime. Read failures flip reading validity, never `sensor_kind`. Reboot re-runs detection (BMP280 probed first, addresses 0x76 → 0x77).

## Entity: History record v2 (on-disk)

```c
typedef struct __attribute__((packed)) {
    uint32_t epoch;        /* UTC seconds                                */
    int16_t  temp_centi;   /* °C × 100                                   */
    uint16_t press_deci;   /* hPa × 10; meaningful only if flags bit1    */
    uint8_t  flags;        /* bit0: temp valid, bit1: pressure recorded  */
    uint8_t  reserved;     /* 0; room for one future field               */
    uint8_t  crc8;         /* CRC-8 poly 0x07 over bytes [0..10]         */
} hist_record_v2_t;        /* sizeof == 12 (static-asserted)             */
```

| Aspect | v1 (existing) | v2 (new) |
|--------|---------------|----------|
| Size | 8 bytes | 12 bytes |
| File name | `YYYYMM.bin` | `YYYYMM.bi2` |
| Fields | epoch, temp, flags(bit0), crc | + press_deci, flags bit1, reserved |
| Written by | pre-feature firmware | this feature onward |
| Read | yes (pressure = absent) | yes |
| Purged | yes (same 3-month rule) | yes |

Encoding rules: `press_deci = round(hPa × 10)`, range check before encode; decode → `float hPa = press_deci / 10.0f`. Records with flags bit1 clear (or any v1 record) report pressure as **absent** (`NAN` at the query callback, `null` in JSON, empty CSV cell) — never zero (FR-006).

**Query callback change** (`history.h`):

```c
typedef void (*history_cb_t)(uint32_t epoch, float temp_c, float pressure_hpa, void *ctx);
/* pressure_hpa is NAN when not recorded */
```

All three existing callers (JSON stream, CSV stream, tests) are updated in this feature.

## JSON shapes (details in [contracts/readings-api.md](./contracts/readings-api.md))

`/api/status` additions:

```json
{
  "pressure_hpa": 1013.2,
  "pressure_valid": true,
  "sensor": "bmp280"
}
```

History record (JSON): `{"timestamp": 1783939200, "temperature": 21.37, "pressure": 1013.2}` — `"pressure": null` when absent.

CSV: `timestamp_iso8601,temperature_c,pressure_hpa` — pressure cell empty when absent.

## i18n additions (all four packs; enforced by `tools/check_i18n.py`)

| Key | en value |
|-----|----------|
| `mgmt_label_pressure` | `Pressure` |
| `mgmt_th_pressure` | `Pressure` |

Unit string "hPa" is fixed markup (SI unit, not translated). Additional keys may be added during implementation if new visible status text appears; the checker keeps the four packs in lockstep.

## BMP280 driver interface (`components/sensor/bmp280.h`)

```c
esp_err_t bmp280_detect(i2c_master_bus_handle_t bus, bmp280_t *out); /* probes 0x76, 0x77; checks chip-id 0x58 */
esp_err_t bmp280_configure(bmp280_t *dev);                           /* osrs_t x2, osrs_p x16, IIR 4, normal mode, standby 1s */
esp_err_t bmp280_read(bmp280_t *dev, float *temp_c, float *press_hpa);

/* Pure compensation (exposed for Unity tests; datasheet §3.11.3 example vector) */
int32_t  bmp280_compensate_t(int32_t raw_t, const bmp280_calib_t *c, int32_t *t_fine); /* 0.01 °C */
uint32_t bmp280_compensate_p(int32_t raw_p, const bmp280_calib_t *c, int32_t t_fine);  /* Pa in Q24.8 */
```

Test vector (datasheet worked example): calibration set from §3.11.3, raw_t=519888, raw_p=415148 → 25.08 °C, 100653.27 Pa (1006.53 hPa).
