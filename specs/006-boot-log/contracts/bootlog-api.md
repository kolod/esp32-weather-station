# Contract: Boot Log API (006)

Applies to the **management server** (`handlers_mgmt.c`) and its emulator mirror
(`tools/hw_emulator.py` `MgmtHandler`). The captive portal does **not** expose this
endpoint.

## GET /api/boot.log

Returns the boot diagnostic log of the most recent boot.

### Request

- Method: `GET`
- No parameters, no body.

### Response — log available

```
HTTP/1.1 200 OK
Content-Type: text/plain; charset=utf-8
```

Body: newline-delimited log lines, ≤ 4096 bytes total, ESP-IDF format:

```
I (1621) sensor: Sensor mode: BMP280 (temperature + pressure)
I (1633) bmp280: BMP280 found at 0x76
I (2104) display: ST7789 panel initialized (250x135)
I (2350) display: UI initialized
```

If the capture overflowed, the final line is:

```
[boot log full - further messages discarded]
```

**Source of truth on device**: `/storage/boot.log`; if the file does not exist yet
(first ~10 s of uptime, or the flush failed) the handler serves the live RAM buffer
instead. View (`fetch`) and download (`<a download="boot.log">`) use this same
endpoint, so their bytes are always identical.

### Response — log not available

File absent/unreadable **and** RAM buffer empty (on the emulator: scenario
`bootlog = "missing"`):

```
HTTP/1.1 404 Not Found
Content-Type: application/json

{"error": "not_found"}
```

The management page must render the localized `mgmt_bootlog_unavailable` message for
any non-200 response or network error (FR-007) — it must never fail to load.

### Guarantees

| # | Guarantee | Spec ref |
|---|-----------|----------|
| 1 | Body ≤ 4096 bytes | FR-004, SC-003 |
| 2 | Content covers the most recent boot only | FR-003 |
| 3 | Contains sensor-detection outcome lines (found / "BMP280 not found at 0x76 or 0x77" / DS18B20 lines) | FR-001, US1-3 |
| 4 | Contains display-initialization outcome lines | FR-002 |
| 5 | Available once the device is network-reachable, even though the events predate the web server (RAM fallback bridges the pre-flush window) | US3 |
| 6 | Endpoint failure modes never affect other pages/endpoints | FR-007/FR-008 |

## Emulator control surface extension

`GET/PUT /emu/scenario` JSON gains:

```json
{ "bootlog": "present" }
```

- `"present"` (default): `h_bootlog` returns a synthetic log whose lines are
  consistent with the active `sensor` scenario (`bmp280` → found lines; `ds18b20` →
  BMP280-not-found + DS18B20-found lines; `none` → both-not-found lines), always
  ≤ 4096 bytes.
- `"missing"`: `h_bootlog` returns the 404 shape above.
- CLI: `--bootlog {present,missing}`, default `present`.

## UI contract (mgmt page)

| Element | Requirement |
|---------|-------------|
| `<section class="card bootlog">` | Present on the main management page (FR-005). |
| `<h2 data-i18n="mgmt_heading_bootlog">` | Localized heading, all 4 languages (FR-009). |
| `<pre id="bootlog-content">` | Preformatted, shows response body verbatim (FR-005). |
| `<a id="btn-bootlog" href="/api/boot.log" download="boot.log">` | Download action (FR-006); hidden when unavailable. |
| Unavailable state | Localized `mgmt_bootlog_unavailable` text instead of `<pre>`/link (FR-007). |
