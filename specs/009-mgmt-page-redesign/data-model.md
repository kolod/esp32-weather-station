# Phase 1 Data Model: Management Page Redesign & Live Readings

No persistent storage is added. The "entities" here are in-memory / on-the-wire shapes
and client-side view state.

## 1. Status snapshot (WebSocket message == `GET /api/status` body)

Built by the shared `build_status_json()` in `web_server`. One flat JSON object, sent
in full on every broadcast. Unchanged from today's `/api/status` — the redesign only
adds a second delivery channel.

| Field | Type | Source | Notes |
|-------|------|--------|-------|
| `temperature_c` | number (°C, 2 dp) | `app_state.reading.value_c` | |
| `temperature_valid` | bool | `app_state.reading.valid` | client shows `---` when false |
| `pressure_hpa` | number (1 dp) | `app_state.pressure.value_hpa` | |
| `pressure_valid` | bool | `app_state.pressure.valid` | |
| `humidity_pct` | number (1 dp) | `app_state.humidity.value_pct` | |
| `humidity_valid` | bool | `app_state.humidity.valid` | |
| `sensor` | enum string | `app_state.sensor_kind` | `none` \| `ds18b20` \| `bmp280` \| `bme280` — drives which quarters/series render |
| `time_synced` | bool | `app_state.time_synced` | |
| `time_source` | enum string | `app_state.time_source` | `none` \| `rtc` \| `ntp` |
| `time_last_sync` | number \| `null` | `rtc_time_last_sync()` | epoch seconds |
| `now` | number | `time(NULL)` | epoch seconds, device clock |
| `tz_name` | string | `settings_get_tz_name()` | IANA name |
| `time_mode` | enum string | `settings_get_time_mode()` | `utc` \| `local` |
| `temp_unit` | enum string | `settings_get_temp_unit()` | `C` \| `F` |
| `wifi.state` | enum string | `app_state.wifi_state` | `idle`\|`provisioning_ap`\|`connecting`\|`connected`\|`retrying`\|`ap_fallback` |
| `wifi.ssid` | string | `esp_wifi_sta_get_ap_info()` | empty unless connected |
| `wifi.rssi` | number | same | dBm |
| `wifi.ip` | string | `esp_netif_get_ip_info()` | dotted quad, empty if none |
| `fw_version` | string | `esp_app_get_description()->version` | header right-edge element |
| `uptime_s` | number | `esp_timer_get_time()` | |
| `history_records` | number | `history_record_count()` | |
| `storage_free_kb` | number | `statvfs("/storage")` | |

**Validation / invariants**:
- Payload MUST fit the existing `char buf[1152]` (no new fields added).
- Field order and formatting MUST be identical between the WS frame and the HTTP
  response (same builder) so the client has one parser.
- Sent as a single text WS frame (`HTTPD_WS_TYPE_TEXT`, not fragmented).

**Broadcast triggers** (`APP_EVENT` ids the broadcaster subscribes to):
`APP_EVT_READING_UPDATED`, `APP_EVT_WIFI_STATE_CHANGED`, `APP_EVT_TIME_SYNCED`,
`APP_EVT_TIME_RESTORED`, `APP_EVT_SETTINGS_CHANGED`.

## 2. WS client registry (firmware, `ws_broadcast.c`)

| Field | Type | Notes |
|-------|------|-------|
| `client_fds` | `int[N]` static array, `N = CONFIG_LWIP_MAX_SOCKETS` | socket descriptors of live WS connections |
| `count` | `size_t` | ≤ `httpd.max_open_sockets` (5) |
| `server` | `httpd_handle_t` | captured in `ws_broadcast_start()` |

**State transitions**:
- **add**: `/api/ws` GET handler, after `httpd_ws` handshake completes → append
  `httpd_req_to_sockfd(req)` if not already present and `count < N`; else close the new
  socket (FR-012).
- **remove**: any `httpd_ws_send_frame_async` returning non-`ESP_OK`, or a received
  CLOSE frame → drop the fd from the array, `count--`.
- No persistence; array is cleared if `mgmt_server_stop()` runs (AP-fallback).

## 3. History series (client-side, from `GET /api/history`)

```
Record        = { timestamp: number/*epoch s*/, temperature: number,
                  pressure: number|null, humidity: number|null }
HistoryResult = { records: Record[] }   // chronological order
```

**Derived view model** after fetch for the selected period:

| Field | Type | Notes |
|-------|------|-------|
| `period` | `'day'\|'week'\|'month'\|'all'` | default `'day'` |
| `from`, `to` | epoch seconds | `to = now`; `from = now - window` (`0` for `all`) |
| `points` | `{ t:number, temp:number, press:number|null, hum:number|null }[]` | after down-sampling |
| `hasPressure` | bool | `records.some(r => r.pressure != null)` → draw pressure series |
| `hasHumidity` | bool | `records.some(r => r.humidity != null)` → draw humidity series |
| `empty` | bool | `records.length === 0` → show empty-state, skip chart |

**Down-sampling rule**: if `records.length > MAX_POINTS` (≈ 400), partition the time
range into `MAX_POINTS` equal buckets; each output point is the mean of that bucket's
records per channel (skip `null`/NaN); empty buckets produce a gap (no point).
Display-only — never mutates data sent to CSV export.

## 4. Client connection state (`mgmt.js`)

| Field | Type | Notes |
|-------|------|-------|
| `ws` | `WebSocket \| null` | current socket |
| `wsState` | `'connecting'\|'open'\|'down'` | drives the fallback poll + optional status line |
| `retryDelay` | number ms | starts 1000, ×2 on each failure, cap 30000, ±20 % jitter |
| `pollTimer` | interval handle | active **only** while `wsState !== 'open'`; 5000 ms |

**Transitions**:
- load → `connecting`; start `pollTimer`; one immediate `refreshStatus()`.
- `onopen` → `open`; clear `pollTimer`; reset `retryDelay` to 1000.
- `onmessage` → `renderStatus(JSON.parse(data))`.
- `onclose`/`onerror` → `down`; ensure `pollTimer` running; `setTimeout(connect, retryDelay)`; then grow `retryDelay`.

## 5. Readings card layout model (presentation only)

2×2 CSS grid inside `.readings`:

```
┌─────────────────────┬─────────────────────┐
│ time  (top-left)    │ temperature (top-r) │
├─────────────────────┼─────────────────────┤
│ pressure (bot-left) │ humidity (bot-right)│
└─────────────────────┴─────────────────────┘
  time-source            (full-width row)
  wifi-status            (full-width row)
```

- Each quarter: a label element + a value element; value shows placeholder
  (`--:--` for time, `---` otherwise) when the corresponding `*_valid` is false or the
  sensor lacks that channel.
- `sensor === 'bme280'` → humidity quarter active; `sensor` in {`bmp280`,`bme280`} →
  pressure quarter active; otherwise those quarters show placeholder but stay in the
  grid (FR-004).
- ≤ ~460 px viewport: grid collapses to one column (`grid-template-columns: 1fr`).
