# Contract: Management Endpoint (emulator port 8080)

Mirror of the firmware HTTPS management server (`handlers_mgmt.c`, `handlers_ota.c`,
`handlers_common.c`). Emulator serves plain HTTP on `http://127.0.0.1:8080`.
Deviations from firmware are marked **[DEV]**.

## Static assets (read from `components/web_server/www/mgmt/` per request)

| Route | Content-Type | Headers |
|-------|--------------|---------|
| `GET /` | `text/html; charset=utf-8` | `Content-Security-Policy: upgrade-insecure-requests; default-src 'self'`, `X-Content-Type-Options: nosniff`, `Cache-Control: no-cache` **[DEV: no HSTS — meaningless without TLS; no-cache for live edit]**; since spec 004 also `Content-Language: <lang>` + `<html lang="…">` patched from `Accept-Language`, same as the portal |
| `GET /mgmt.css` | `text/css` | `Cache-Control: no-cache` **[DEV: device sends no cache header here]** |
| `GET /mgmt.js` | `application/javascript` | `Cache-Control: no-cache` |
| `GET /i18n/<lang>.json` | `application/json` | **[implemented in firmware since spec 004 — see `specs/004-fix-web-i18n/contracts/i18n-http.md`]** |
| `GET /i18n.js` | `application/javascript` | serves `www/common/i18n.js` (shared i18n applier; spec 004) |

## `GET /api/status` → 200 `application/json`

```json
{
  "temperature_c": 21.37,
  "temperature_valid": true,
  "time_synced": true,
  "time_source": "ntp",
  "time_last_sync": 1783939200,
  "now": 1783942800,
  "tz_name": "UTC",
  "time_mode": "local",
  "temp_unit": "C",
  "wifi": { "state": "connected", "ssid": "HomeNet", "rssi": -47, "ip": "192.168.1.42" },
  "fw_version": "emu-0.1.0",
  "uptime_s": 128,
  "history_records": 1440,
  "storage_free_kb": 1024
}
```

- `time_source` ∈ `none|rtc|ntp`; `time_last_sync` epoch or `null`.
- `wifi.state` ∈ `idle|provisioning_ap|connecting|connected|retrying|ap_fallback`; when not `connected`: `ssid:""`, `rssi:0`, `ip:""`.
- Scenario effects: `sensor_valid=false` → `temperature_valid:false`; `time_synced=false` → `time_synced:false, time_source:"none", time_last_sync:null`; `wifi_connected=false` → wifi block per above.

## `PUT /api/config`

Request (any subset; flat JSON strings):

```json
{ "tz_name": "Europe/Kyiv", "time_mode": "utc", "temp_unit": "F" }
```

- 200: full `/api/status` body reflecting the new values (firmware calls `api_status`).
- 400 `{"error":"unknown_timezone"}` — `tz_name` not in catalog.
- 400 `{"error":"invalid_time_mode"}` — not `utc`/`local`.
- 400 `{"error":"invalid_temp_unit"}` — not `C`/`F`.
- Fields are applied in order (tz, mode, unit); first failure stops processing (firmware behavior).

## `GET /api/history[?from=<epoch>&to=<epoch>]` → 200

```json
{ "records": [ { "timestamp": 1783939200, "temperature": 21.30 }, ... ] }
```

Ascending by timestamp; inclusive bounds; missing params default 0 / max-u32;
temperatures 2 decimal places.

## `GET /api/history.csv` → 200 `text/csv`

Headers: `Content-Disposition: attachment; filename="history.csv"`.

```csv
timestamp_iso8601,temperature_c
2026-07-12T09:20:00Z,21.30
```

Same record set as `/api/history` (unfiltered), UTC ISO-8601 `Z` timestamps, 2 decimals.

## `POST /api/ota` (raw firmware image body)

| Condition | Response |
|-----------|----------|
| Session already `receiving` | `409` `{"error":"update_in_progress"}` |
| `Content-Length` ≤ 0 or > 3 145 728 | `507` `{"error":"image_too_large"}` |
| Scenario `invalid_image` (after body consumed) | `400` `{"error":"invalid_image","message":"Image validation failed"}` |
| Scenario `write_error` | `500` (session `failed`, error `write_error`) |
| Success | `200` `{"status":"applied","reboot_in_s":3}` |

Progress: `progress_pct` advances with bytes received (poll `/api/ota/status` during upload).
After success + 3 s: simulated reboot — uptime resets, `fw_version` suffix bumps, OTA state → `idle` **[DEV: device actually reboots and drops connections]**.

## `GET /api/ota/status` → 200

```json
{ "state": "idle", "progress_pct": 0, "error": null }
```

`state` ∈ `idle|receiving|validating|applied_pending_reboot|failed`; `error` string only when `failed` (`invalid_image` | `write_error` | `set_boot_failed`).

## `GET /api/timezones` → 200

```json
[ { "name": "UTC" }, { "name": "Europe/Kyiv" }, ... ]
```

`Cache-Control: max-age=3600` (harmless: content is static per run).

## Errors

- Unknown route → `404` and a console log line (`MISS GET /api/xyz`).
- 400-level bodies above are sent as JSON text with the shown status **[DEV: firmware's `httpd_resp_send_err` wraps the message in an HTML error page; pages only check status, so JSON body is safe and more debuggable]**.
