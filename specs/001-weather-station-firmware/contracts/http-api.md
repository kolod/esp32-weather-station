# Contract: HTTP/HTTPS API

**Feature**: 001-weather-station-firmware | **Data model**: [../data-model.md](../data-model.md)

Two server instances (see research.md R5):

- **Portal server** — HTTP :80, active only in AP-fallback/provisioning mode (192.168.4.1)
- **Management server** — HTTPS :443, active only when STA-connected; cert from owner CA. Optional :80 → 301 https redirect.

All JSON responses: `Content-Type: application/json; charset=utf-8`. Timestamps are UTC epoch seconds. Temperatures are °C with 0.01 resolution (clients convert to °F for display).

## Portal endpoints (HTTP, AP mode)

### `GET /` — setup page
- Localized per `Accept-Language` (en/de/fr/uk, q-values honored, fallback en — FR-022). Response includes `Content-Language` header.
- Page contains: SSID text field + scan-result picker, password field, timezone `<select>` (from `GET /api/timezones`), submit, status area.

### Captive-portal probe handlers
`GET /generate_204`, `/gen_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt`, and any unknown Host → `302 Location: http://192.168.4.1/`. `captive_dns` resolves every A-query to 192.168.4.1.

### `GET /api/scan`
→ `200` `{"networks":[{"ssid":"...","rssi":-55,"secure":true},…]}` (deduped, sorted by RSSI desc, max 20)

### `POST /api/wifi`
Body: `{"ssid":"...","password":"...","tz_name":"Europe/Kyiv"}`
- `202` `{"status":"connecting"}` — join attempt started (AP stays up, APSTA)
- `400` — missing/oversize fields (ssid >32 B, password >63 B, unknown tz_name)

### `GET /api/wifi/status`
→ `200` `{"state":"connecting"|"connected"|"failed","ip":"192.168.1.23"|null,"reason":"auth"|"not_found"|null}`
- Portal page polls this after submit; on `connected` shows success + device URL `https://weather-XXXX.local`, then device stops AP within 10 s (FR-009). On `failed` the form is re-enabled (FR-008).

## Management endpoints (HTTPS, STA mode)

### `GET /` — management page (English, v1)
Shows: current temperature + validity, time + sync state, WiFi status/RSSI, firmware version, timezone setting, history view/download, OTA upload.

### `GET /api/status`
```json
{
  "temperature_c": 23.42, "temperature_valid": true,
  "time_synced": true, "now": 1783190400,
  "tz_name": "Europe/Kyiv", "time_mode": "local", "temp_unit": "C",
  "wifi": {"state": "CONNECTED", "ssid": "home", "rssi": -58, "ip": "192.168.1.23"},
  "fw_version": "1.0.0", "uptime_s": 86400,
  "history_records": 12960, "storage_free_kb": 9600
}
```

### `GET /api/timezones`
→ `200` `[{"name":"UTC"},{"name":"Europe/Kyiv"},…]` (compiled-in table; also used by portal)

### `PUT /api/config`
Body (any subset): `{"tz_name":"...","time_mode":"local"|"utc","temp_unit":"C"|"F"}`
→ `200` new effective config | `400` unknown value. Persisted atomically (FR-019); display reflects change ≤ 2 s.

### `GET /api/history?from=<epoch>&to=<epoch>`
→ `200` `{"records":[{"timestamp":1783190400,"temperature":23.42},…]}` — chronological; invalid samples omitted; unbounded query = full 3-month window, streamed/chunked (FR-024).

### `GET /api/history.csv`
→ `200` `text/csv`, `Content-Disposition: attachment` — `timestamp_iso8601,temperature_c` rows; download deliverable of US6.

### `POST /api/ota`
- Body: raw `application/octet-stream` firmware image (Content-Length required, ≤ 3 MB).
- `200` `{"status":"applied","reboot_in_s":3}` — validated and boot partition set; device reboots.
- `400` `{"error":"invalid_image"}` — magic/app-descriptor/`esp_ota_end` validation failed; running firmware untouched (FR-015).
- `409` `{"error":"update_in_progress"}`.
- `507` `{"error":"image_too_large"}`.

### `GET /api/ota/status`
→ `200` `{"state":"idle"|"receiving"|"validating"|"applied_pending_reboot"|"failed","progress_pct":42,"error":null}` — polled by the page for FR-017.

## Error envelope

Non-2xx JSON bodies: `{"error":"<machine_code>","message":"<human text>"}`.

## Contract invariants

- Portal server never serves management endpoints and vice versa (mode-gated registration).
- `device.key` and WiFi password are never emitted by any endpoint.
- All state-changing endpoints exist only on the HTTPS server, except `POST /api/wifi` which is the purpose of the portal.
