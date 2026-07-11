# Data Model: ESP32 Weather Station Firmware

**Date**: 2026-07-11 | **Plan**: [plan.md](./plan.md) | **Storage details**: [contracts/storage-schema.md](./contracts/storage-schema.md)

## Entities

### 1. NetworkCredentials

Persisted by `esp_wifi` itself (wifi NVS namespace, `WIFI_STORAGE_FLASH`).

| Field | Type | Constraints |
|---|---|---|
| ssid | string | 1–32 bytes, UTF-8; empty ⇒ "not provisioned" |
| password | string | 0–63 bytes (0 = open network) |

**Lifecycle**: created/replaced only via captive portal submit; cleared by factory reset (left button 5 s long-press). Never exposed back through any API (write-only from portal).

### 2. DeviceSettings (NVS namespace `settings`)

| Key | Type | Default | Constraints |
|---|---|---|---|
| `tz_name` | string | `"UTC"` | must exist in compiled-in timezone table |
| `tz_posix` | string | `"UTC0"` | POSIX TZ with DST rule, derived from `tz_name` |
| `time_mode` | u8 | 0 (local) | 0=local, 1=UTC (left button toggle, FR-025) |
| `temp_unit` | u8 | 0 (°C) | 0=Celsius, 1=Fahrenheit (right button toggle, FR-026) |

**Rules**: every write is a single NVS commit (atomic, FR-019). Writes emit `SETTINGS_CHANGED` event; display and web read current values on event.

### 3. TemperatureReading (RAM, `app_state`)

| Field | Type | Notes |
|---|---|---|
| value_c | float | latest probe value, °C |
| valid | bool | false on CRC error / probe absent / out of range (−55…+125) |
| updated_at | int64 | epoch ms of last successful read |

**Rules**: written only by `sensor` task every 5 s; consumers must check `valid` and render "unavailable" when false (FR-003). °F conversion happens at render time only — storage is always °C.

### 4. HistoryRecord (littlefs, monthly files)

8-byte packed record, appended hourly in batches of ≤12 (FR-023).

| Field | Type | Notes |
|---|---|---|
| epoch | u32 | UTC seconds; sample times aligned to 5-min grid |
| temp_centi | i16 | °C × 100; range −5500…+12500 |
| flags | u8 | bit0: valid; bits1–7 reserved = 0 |
| crc8 | u8 | CRC-8 over previous 7 bytes |

**Aggregate**: `HistoryLog` = ordered set of monthly files `YYYYMM.bin`; invariant: total span ≤ 3 months + current month; purge = delete whole out-of-window files, daily.

### 5. DeviceIdentity (derived, immutable)

| Field | Source |
|---|---|
| mac | `esp_efuse_mac_get_default()` |
| suffix | last 2 MAC bytes, 4 lowercase hex chars |
| hostname | `weather-{suffix}` (mDNS: `weather-{suffix}.local`) |
| ap_ssid | `weather-{suffix}` |

### 6. DeviceCertificate (littlefs `/storage/certs/`)

| File | Content | Constraints |
|---|---|---|
| `device.crt` | PEM cert, EC P-256, signed by owner CA | SAN must include `DNS:weather-{suffix}.local` |
| `device.key` | PEM private key | never leaves device; not readable via any API |

**Lifecycle**: written by provisioning tool (or re-issued via management upload); absence ⇒ HTTPS server refuses to start and management is unavailable until provisioned (portal still works — it is HTTP).

### 7. FirmwareImage / UpdateSession

| Field | Type | Notes |
|---|---|---|
| state | enum | idle → receiving → validating → applied_pending_reboot / failed |
| progress_pct | u8 | 0–100 while receiving |
| error | string | reason on `failed` |

**State transitions**: `idle→receiving` on `POST /api/ota` start; `receiving→failed` on write/validation error (running firmware untouched, FR-015); `receiving→validating→applied_pending_reboot` on success; post-reboot self-check passes ⇒ image marked valid, else bootloader rollback (FR-016). Only one session at a time (409 on concurrent start).

### 8. WifiState (RAM, `app_state`)

enum: `PROVISIONING_AP` | `CONNECTING` | `CONNECTED` | `RETRYING` | `AP_FALLBACK` — drives status icon on display and `/api/status`; transitions per R4 in research.md.

## Events (`esp_event`, custom base `APP_EVENT`)

| Event | Payload | Emitted by → consumed by |
|---|---|---|
| `READING_UPDATED` | none (read app_state) | sensor → display, history |
| `TIME_SYNCED` | none | wifi_mgr/SNTP → display, history |
| `SETTINGS_CHANGED` | changed-key mask | settings → display, web_server |
| `WIFI_STATE_CHANGED` | new WifiState | wifi_mgr → display, web_server |
| `OTA_STATE_CHANGED` | UpdateSession snapshot | web_server → display (status icon) |
