# Contract: On-Device Storage Schema

**Feature**: 001-weather-station-firmware | **Data model**: [../data-model.md](../data-model.md)

## Partition table (16 MB flash) — `partitions.csv`

| Name | Type | SubType | Offset | Size | Purpose |
|---|---|---|---|---|---|
| nvs | data | nvs | 0x9000 | 64 KB | WiFi creds (esp_wifi), settings |
| otadata | data | ota | 0x19000 | 8 KB | active-slot + rollback bookkeeping |
| phy_init | data | phy | 0x1B000 | 4 KB | RF calibration |
| ota_0 | app | ota_0 | 0x20000 | 3 MB | firmware slot A |
| ota_1 | app | ota_1 | 0x320000 | 3 MB | firmware slot B |
| storage | data | littlefs | 0x620000 | 0x9E0000 (≈9.9 MB) | certs + history |

`sdkconfig.defaults` must set: `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_PARTITION_TABLE_CUSTOM=y`, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`.

## NVS layout

### Namespace `nvs.net80211` (owned by esp_wifi — do not touch directly)
Persisted STA config via `esp_wifi_set_storage(WIFI_STORAGE_FLASH)` + `esp_wifi_set_config()`. "Credentials exist" test: `esp_wifi_get_config()` returns non-empty SSID. Factory reset: `esp_wifi_restore()`.

### Namespace `settings` (owned by `settings` component)

| Key | NVS type | Default | Values |
|---|---|---|---|
| `tz_name` | str | `UTC` | key into timezone table |
| `tz_posix` | str | `UTC0` | POSIX TZ incl. DST rule, e.g. `EET-2EEST,M3.5.0/3,M10.5.0/4` |
| `time_mode` | u8 | 0 | 0=local, 1=UTC |
| `temp_unit` | u8 | 0 | 0=°C, 1=°F |

Each logical change = one `nvs_set_*` + `nvs_commit` (atomic per FR-019).

## LittleFS `storage` partition

```text
/storage
├── certs/
│   ├── device.crt     # PEM, EC P-256, CA-signed, SAN: DNS:weather-XXXX.local
│   └── device.key     # PEM EC private key, mode: device-internal only
└── history/
    ├── 202604.bin     # oldest retained month (rolling 3-month window)
    ├── 202605.bin
    ├── 202606.bin
    └── 202607.bin     # current month, appended hourly
```

### History file format (`YYYYMM.bin`)

- No header; file = sequence of 8-byte little-endian records (name carries the month).
- Record: `u32 epoch` (UTC s) · `i16 temp_centi` (°C×100) · `u8 flags` (bit0 valid) · `u8 crc8` (poly 0x07 over bytes 0–6).
- Append-only, ≤12 records per hourly flush (FR-023: one write op/hour). Reader skips records failing CRC (torn tail after power loss — acceptable, littlefs guarantees file metadata consistency).
- Capacity check: 31 d × 288 rec × 8 B ≈ 71 KB/month; 4 files ≈ 285 KB ≪ 9.9 MB.

### Purge contract (daily, 00:30 local)

Delete every `history/YYYYMM.bin` whose **last possible record** (end of month) is older than `now − 3 months`. Whole-file `unlink` only — never rewrite (FR-023). Result: retained span always ≥ 3 months, ≤ 3 months + current month.

## Certificate provisioning contract (`tools/ca/`)

| Script | Input | Output |
|---|---|---|
| `ca-create` | org name | `ca.key` (stays on owner machine), `ca.crt` (install on clients) |
| `device-issue <suffix>` | `ca.key`, `ca.crt` | `device.crt` (SAN `DNS:weather-<suffix>.local`, 825 d), `device.key` |
| `device-provision <port>` | cert+key | littlefs image containing `/certs/*` flashed to `storage` partition |

Invariants: CA key never copied to a device; `device.key` never leaves the device after provisioning; re-provisioning must not touch `history/` (image build merges, or runtime upload writes only `certs/`).
