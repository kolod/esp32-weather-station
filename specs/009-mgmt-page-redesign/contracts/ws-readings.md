# Contract: `GET /api/ws` — live readings WebSocket

**Server**: HTTPS management server only (`wss://<host>/api/ws`, port 443, device
cert). Plain `ws://` and any non-TLS listener MUST NOT expose this route.

**Requires**: `CONFIG_HTTPD_WS_SUPPORT=y`.

## Handshake

- Client: standard WebSocket upgrade request to `/api/ws` over TLS.
- Server: `httpd` URI handler registered with `.method = HTTP_GET`,
  `.is_websocket = true`. On successful upgrade the server records the client socket fd
  in the broadcast registry.
- **Capacity**: if the registry already holds `httpd.max_open_sockets - 1` WS clients,
  the server completes the upgrade then immediately sends a CLOSE frame (code 1013,
  "try again later") and drops the fd. Existing clients are unaffected (FR-012).
- No subprotocol, no auth token beyond the TLS session (management LAN trust model,
  unchanged from the rest of `/api/*`).

## Messages: server → client

- Type: text frame, unfragmented.
- Payload: the **status snapshot** JSON object — byte-identical to the body of
  `GET /api/status` (same `build_status_json()` builder). See
  [../data-model.md](../data-model.md) §1 for the field list.
- Cadence: one frame per relevant `APP_EVENT` —
  `READING_UPDATED` (≈ every 5 s), `WIFI_STATE_CHANGED`, `TIME_SYNCED`,
  `TIME_RESTORED`, `SETTINGS_CHANGED`.
- On connect: the client SHOULD call `GET /api/status` once for immediate paint; the
  server does not send an unsolicited initial frame (kept simple — next sensor tick
  arrives within ~5 s).

## Messages: client → server

- The client sends nothing in normal operation.
- The server MUST tolerate and ignore any received text/binary frame.
- PING/PONG: handled by `esp_http_server` defaults; client relies on browser keepalive.
- CLOSE frame from client → server removes the fd from the registry.

## Error / lifecycle

| Event | Server behavior | Client behavior |
|-------|-----------------|-----------------|
| Send returns non-`ESP_OK` | remove fd from registry, close socket | sees socket close → reconnect w/ backoff |
| Client TCP drop / page close | fd pruned on next send attempt | n/a |
| `mgmt_server_stop()` (AP fallback) | all WS sockets closed with server | reconnect attempts fail until STA restored, backoff caps at 30 s |
| Server socket pool exhausted | LRU purge (httpd default) may close idle WS | client reconnects |

## Client fallback (normative for FR-008 / SC-004)

If the socket is not `OPEN`, the client MUST poll `GET /api/status` at least every 5 s
and MUST keep attempting to reconnect the WebSocket with exponential backoff
(1 s → 30 s cap, jittered). When the socket reopens, polling stops.

## Security

- Inherits HSTS + `default-src 'self'` CSP from `mgmt.html`; `wss://` same-origin is
  permitted by `default-src`.
- Payload contains no secrets (no WiFi password, no key material) — same fields already
  public on `/api/status`.
