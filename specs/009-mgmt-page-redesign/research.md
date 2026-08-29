# Phase 0 Research: Management Page Redesign & Live Readings

## 1. WebSocket transport on the HTTPS management server

**Decision**: Enable `CONFIG_HTTPD_WS_SUPPORT=y` and register a single URI handler
`GET /api/ws` with `.is_websocket = true` on the existing `httpd_ssl` server started in
`mgmt_server.c`. Clients connect to `wss://<host>/api/ws`.

**Rationale**:
- The management server is already an `esp_https_server` instance; registering the WS
  handler there gives WSS for free (device cert, HSTS, HTTP→HTTPS rejection) with no
  new listener — satisfies Principle V.
- `esp_http_server`'s built-in WS support handles the HTTP Upgrade handshake and frame
  framing; the project already depends on `esp_http_server` / `esp_https_server`.
- CSP `default-src 'self'` (no explicit `connect-src`) permits a same-origin
  `wss://` connection, so `mgmt.html`'s existing policy needs no change.

**Alternatives considered**:
- **Server-Sent Events (EventSource)**: simpler (plain chunked HTTP response, no
  handshake, auto-reconnect built in). Rejected because the spec explicitly requires a
  WebSocket, and SSE consumes a socket per client just the same.
- **Long-poll `/api/status`**: no new config, but keeps ~1 request/interval of latency
  and doubles request volume. Rejected — does not meet SC-002 (< 1 s).
- **Second plaintext `httpd` on :81 for WS**: avoids TLS cost per WS client. Rejected —
  Principle V forbids plaintext management transport.

## 2. Broadcasting readings to all connected clients

**Decision**: New `ws_broadcast.c` in `web_server`. On `ws_broadcast_start(server)` it:
1. keeps a fixed-size static array `int client_fds[CONFIG_LWIP_MAX_SOCKETS]` of active
   WS socket descriptors (added on handshake in the `/api/ws` handler, pruned on send
   failure);
2. registers one `esp_event` handler for `APP_EVENT` covering `APP_EVT_READING_UPDATED`,
   `APP_EVT_WIFI_STATE_CHANGED`, `APP_EVT_TIME_SYNCED`, `APP_EVT_TIME_RESTORED`,
   `APP_EVT_SETTINGS_CHANGED`;
3. on any of those, builds the status JSON once and, for each tracked fd, calls
   `httpd_queue_work(server, send_cb, ctx)` so the actual `httpd_ws_send_frame_async`
   runs on the httpd worker (never from the event-loop task, which has a small stack).

**Rationale**:
- Re-uses the existing event fan-out (`app_event_post(APP_EVT_READING_UPDATED)` is
  already emitted by `sensor_task` every ~5 s) — the "faster" gain is removing the
  client's 0–5 s poll wait, delivering each sample the instant it exists (Assumptions
  in spec: sampling interval unchanged).
- `httpd_queue_work` is the ESP-IDF-sanctioned way to push data to WS clients outside
  a request context and serializes sends on the server task.
- One shared payload buffer per broadcast; O(clients) enqueues, clients ≤ cap.

**Alternatives considered**:
- Dedicated broadcaster task with its own queue: extra task + stack (Principle IV).
  Rejected — the event loop + `httpd_queue_work` already provides the needed
  decoupling.
- Sending only a delta / only changed fields: marginal bandwidth saving, more code and
  client complexity. Rejected — full status JSON is ~1 KB and already rendered
  idempotently by `renderStatus()` (FR-010 is a *DOM* no-flicker requirement, met on
  the client, not a wire requirement).

## 3. Shared status-JSON builder

**Decision**: Extract the body of `api_status()` into
`esp_err_t build_status_json(char *buf, size_t buf_len, size_t *out_len)` (new
`status_json.c/.h`, or a non-static function in `handlers_mgmt.c` exposed via
`handlers_mgmt.h`). `api_status()` becomes a thin wrapper; `ws_broadcast.c` calls the
same builder.

**Rationale**: Single source of truth for the payload shape — the WS message and
`GET /api/status` (the documented fallback) must stay byte-identical so the client has
one `renderStatus()` path (FR-008, contract ws-readings.md).

**Alternatives considered**: Duplicating the `snprintf` block in the broadcaster —
rejected, guaranteed to drift.

## 4. Socket budget and mbedTLS memory

**Decision**: Raise `cfg.httpd.max_open_sockets` from 2 to **5** in `mgmt_server.c`.
Keep `CONFIG_LWIP_MAX_SOCKETS` at its default (10 — already ≥ 5 + control sockets).
Enable `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` (and, if the build's static footprint allows,
leave content-length at default) to blunt the per-session RAM cost.

**Rationale**:
- A WS client permanently occupies one socket; the cap of 2 cannot serve one live page
  plus any concurrent asset/CSV request, let alone the ≤ 4 viewers the spec assumes.
- `esp_http_server` also uses internal control sockets; 5 user sockets + LWIP default
  headroom is safe.
- mbedTLS in/out content buffers (default 16 KB each) dominate per-session heap;
  `CONFIG_MBEDTLS_DYNAMIC_BUFFER` frees them between records and is the standard
  mitigation on no-PSRAM ESP32.
- This is the one tracked Principle IV deviation (plan.md Complexity Tracking).

**Validation required**: after the sdkconfig change, run `idf.py build` and, on device
or emulator, open 3 pages + trigger a CSV download and confirm no `httpd` socket-
exhaustion warnings and free heap stays > 40 KB (SC-006 headroom).

**Alternatives considered**:
- Reducing `CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN` to 4–8 KB: smaller footprint but
  risks breaking the OTA upload path and large i18n responses. Rejected as first
  choice; dynamic buffers are safer. Revisit only if heap validation fails.
- Idle-timeout WS clients aggressively: helps churn but not the steady-state of
  multiple real viewers. Kept as a secondary safeguard (`httpd` LRU purge is default).

## 5. Client reconnect / fallback strategy

**Decision** (in `mgmt.js`):
- On load, open `new WebSocket('wss://' + location.host + '/api/ws')`.
- `onmessage`: `JSON.parse` → `renderStatus(status)` (same function the poll used).
- `onclose` / `onerror`: schedule reconnect with exponential backoff starting 1 s,
  doubling to a 30 s cap, with ±20 % jitter.
- Fallback poll: keep a `setInterval(refreshStatus, 5000)` that runs **only while the
  socket is not OPEN**; started on load, cleared on `onopen`, restarted on `onclose`.
  This guarantees SC-004 (≥ 1 update / 10 s) even if WS never connects.
- One initial `refreshStatus()` on load so the page is populated before the socket
  opens.

**Rationale**: Reuses the entire existing render path; backoff prevents connection
storms on a flapping network (spec Edge Cases); poll-only-when-down avoids double
updates and needless requests.

**Alternatives considered**: `reconnecting-websocket` library — external dependency,
rejected (Principle IV). Fixed 5 s retry — connection storm risk, rejected.

## 6. History plot rendering

**Decision**: New `www/common/chart.js` exporting a single
`drawTimeSeries(canvas, series, opts)` that renders directly on a `<canvas>` 2D
context: axes, gridlines, time labels, and one polyline per series (temperature always;
pressure and humidity when present, on a secondary right axis / normalized). Redraw on
period change and on `resize`.

**Rationale**:
- CSP `default-src 'self'` and Principle IV rule out Chart.js / uPlot / d3 from a CDN,
  and vendoring a full charting lib (30–250 KB) into flash is disproportionate for one
  line plot.
- A dependency-free canvas renderer for 1–3 series with a shared time axis is ~150–250
  lines and fully under project control.
- Mirrors the existing `www/common/i18n.js` "shared vanilla helper" pattern.

**Alternatives considered**:
- Vendored uPlot (~40 KB min): smallest real library, still an external artifact to
  track/update and larger than the hand-rolled need. Rejected for now; revisit if
  requirements grow (zoom, tooltips, pan).
- SVG polyline instead of canvas: fine for Day/Week, but "All" can be thousands of
  points — canvas + down-sampling is cheaper. Rejected.

## 7. Period selector and down-sampling

**Decision**: Selector options map to a lookback window computed client-side from
`Date.now()`:
| Option | Window | Default |
|--------|--------|---------|
| Day    | now − 24 h | ✅ |
| Week   | now − 7 d  | |
| Month  | now − 30 d | |
| All    | `from=0`   | |

Fetch `GET /api/history?from=<epoch>&to=<epoch>` (existing endpoint, unchanged). If the
returned record count exceeds a display budget (~400 points), bucket the records by
time and average each bucket before drawing (temperature/pressure/humidity averaged
independently, NaN-skipping). Down-sampling is display-only; CSV export
(`/api/history.csv`) and stored data are untouched (FR-018, FR-021, SC-007).

**Rationale**: `/api/history` already accepts `from`/`to` and streams JSON records with
`pressure`/`humidity` possibly `null`; no firmware change needed for the plot data
path. Averaging in buckets keeps the "All" canvas draw and the DOM light (SC-005/006).

**Empty period**: if `records.length === 0`, `chart.js` draws nothing and `mgmt.js`
shows a localized empty-state message element instead (FR-017).

**Alternatives considered**: server-side down-sampling (new query param) — more
firmware, more test surface, and the device would do float math per request. Rejected;
client CPU is abundant.

## 8. Header caption / firmware-version alignment

**Decision**: Wrap the `<header>` inner content in a `<div class="header-inner">` that
carries the **same** `max-width` and horizontal `padding` as `main` (`max-width:780px;
padding:0 16px; margin:0 auto`). Inside it, `display:flex; justify-content:space-between`
puts the caption flush-left and `#fw-version` flush-right, exactly over the card content
box. On narrow screens the flex row wraps / shrinks; both stay visible (FR-025).

**Rationale**: Pure CSS, no JS measurement; alignment is structural so it holds at every
width down to 360 px (SC-009).

**Alternatives considered**: JS that measures `.card` offset and sets header padding —
fragile, reflow-sensitive. Rejected.

## 9. i18n key changes

**Decision**:
- **Add** (all four packs): `mgmt_period_day`, `mgmt_period_week`, `mgmt_period_month`,
  `mgmt_period_all`, `mgmt_legend_temperature`, `mgmt_legend_pressure`,
  `mgmt_legend_humidity`, `mgmt_history_empty`, `mgmt_ws_reconnecting` (optional status
  line).
- **Remove** (now unused): `mgmt_btn_load_hist`, `mgmt_btn_bootlog`, `mgmt_th_time`,
  `mgmt_th_temperature`, `mgmt_th_pressure`, `mgmt_th_humidity` — unless the plot legend
  reuses `mgmt_th_*`; decision: introduce fresh `mgmt_legend_*` keys and delete all
  `mgmt_th_*` + the two `mgmt_btn_*`.
- **Rename**: `mgmt_heading_history` value from "Temperature History" → "History" (plot
  now shows multiple series) in all packs.
- Run `python tools/check_i18n.py` — must exit 0 (parity + no missing keys; unused-key
  warnings acceptable but should be zero after cleanup).

**Rationale**: `tools/check_i18n.py` fails the workflow if HTML/JS reference a key
absent from `en.json` or if packs diverge; keeping the four packs in lockstep is a
spec requirement (FR-028, SC-008).

## 10. Boot-log card

**Decision**: Delete the `<a id="btn-bootlog">` element from `mgmt.html`, its CSS
rule, and the `$('btn-bootlog').classList.add('hidden')` line in `mgmt.js`'s catch
block. Keep the inline `<pre id="bootlog-content">` fetch of `/api/boot.log` and the
`#bootlog-unavailable` fallback unchanged. `GET /api/boot.log` stays registered (still
the data source for the inline view).

**Rationale**: FR-026/FR-027 — remove the button only; inline behavior and the endpoint
are retained.

## Open questions

None. All spec `[NEEDS CLARIFICATION]` were resolved at specify time; the socket-count
increase is the only constitution deviation and is tracked with a validation step.
