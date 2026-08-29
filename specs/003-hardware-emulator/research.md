# Research: Local Hardware Emulator for Web UI Testing

**Feature**: 003-hardware-emulator | **Date**: 2026-07-12

All findings below come from direct inspection of the firmware web component
(`components/web_server/`), which is the source of truth the emulator must mirror.

## R1 — HTTP stack

- **Decision**: Python stdlib `http.server.ThreadingHTTPServer` with a custom `BaseHTTPRequestHandler` subclass; two server instances (mgmt, portal) each on its own port, both bound to `127.0.0.1`; shared in-memory state guarded by `threading.Lock`.
- **Rationale**: User constraint explicitly forbids big frameworks. The API surface is small (~16 routes), all responses are small JSON/text, and `ThreadingHTTPServer` handles concurrent tabs polling (spec edge case) without any dependency. A stock Python 3 install is the only prerequisite.
- **Alternatives considered**: Flask/FastAPI (rejected: external dependency, violates constraint); `asyncio` + `aiohttp` (rejected: dependency); raw `socket` (rejected: needless work — `http.server` is sufficient and standard).

## R2 — Ports and binding

- **Decision**: Management on `http://127.0.0.1:8080`, portal on `http://127.0.0.1:8081`; overridable with `--mgmt-port` / `--portal-port`. Bind strictly to `127.0.0.1` (clarification: localhost only, no LAN option). If a port is taken, exit with a message naming the port and the override flag.
- **Rationale**: The real device runs the two servers at different times on standard ports (portal HTTP :80 in AP mode, mgmt HTTPS :443 in STA mode); the spec requires both simultaneously, so two distinct dev ports are needed. 8080/8081 are conventional dev ports rarely privileged.
- **Alternatives considered**: Ports 80/443 for fidelity (rejected: privileged on Unix, collision-prone, TLS complexity); single port with path prefix (rejected: pages use absolute paths like `/api/status`, would require modifying page code — forbidden).

## R3 — No TLS in the emulator

- **Decision**: Plain HTTP for both endpoints.
- **Finding**: `mgmt_server.c` starts `esp_https_server` on :443 with a device certificate from LittleFS, and `mgmt_page` sets `Strict-Transport-Security` and a CSP header. However, both pages reference assets and APIs exclusively via relative/root-relative URLs, so scheme does not affect page behavior.
- **Rationale**: Self-signed local TLS adds browser-warning friction and zero testing value for page logic. The emulator replicates the mgmt response *headers* (CSP, `X-Content-Type-Options: nosniff`, HSTS omitted) so CSP-related page breakage still surfaces locally — except HSTS, which is actively harmful on localhost.
- **Alternatives considered**: `ssl.SSLContext` with a generated self-signed cert (rejected: browser warnings slow the loop; no benefit for page testing).

## R4 — Endpoint inventory to emulate (from firmware source)

**Management server** (`handlers_mgmt.c`, `handlers_ota.c`, `handlers_common.c`):

| Route | Method | Firmware behavior |
|-------|--------|-------------------|
| `/` | GET | mgmt.html (gzip embedded; emulator serves plain from disk) + CSP/nosniff headers |
| `/mgmt.css`, `/mgmt.js` | GET | static assets |
| `/api/status` | GET | JSON snapshot (see contracts/mgmt-api.md) |
| `/api/config` | PUT | partial update of `tz_name`/`time_mode`/`temp_unit`; 400 with `{"error":"unknown_timezone"|"invalid_time_mode"|"invalid_temp_unit"}`; on success returns the full `/api/status` body |
| `/api/history` | GET | `?from=&to=` epoch filters; `{"records":[{"timestamp":u32,"temperature":f2}...]}` |
| `/api/history.csv` | GET | `text/csv`, `Content-Disposition: attachment`, header `timestamp_iso8601,temperature_c`, ISO-8601 UTC rows |
| `/api/ota` | POST | raw firmware body; 409 `update_in_progress`, 507 `image_too_large` (>3 MB), 400 `invalid_image`, 500 `write_error`; success `{"status":"applied","reboot_in_s":3}` then reboots after 3 s |
| `/api/ota/status` | GET | `{"state":"idle|receiving|validating|applied_pending_reboot|failed","progress_pct":n,"error":str|null}` |
| `/api/timezones` | GET | JSON array `[{"name":"..."}]`, `Cache-Control: max-age=3600` |

**Portal server** (`portal_server.c`, `handlers_common.c`):

| Route | Method | Firmware behavior |
|-------|--------|-------------------|
| `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt` | GET | 302 → `http://192.168.4.1/` (captive probe) |
| `/` | GET | index.html + `Content-Language` from Accept-Language, `Cache-Control: no-cache` |
| `/portal.css`, `/portal.js` | GET | static assets, `max-age=3600` |
| `/api/scan` | GET | `{"networks":[{"ssid":s,"rssi":n,"secure":bool}...]}`, ≤20 entries |
| `/api/wifi` | POST | JSON `{ssid,password,tz_name?}`; 202 `{"status":"connecting"}`; 400 `empty_body`/`invalid_ssid` (empty or >32 chars)/`password_too_long` (>63) |
| `/api/wifi/status` | GET | `{"state":"connecting","ip":null,"reason":null}` / `{"state":"connected","suffix":"XXXX","reason":null}` / `{"state":"failed","ip":null,"reason":"auth"|"not_found"|null}` |
| `/api/timezones` | GET | same as mgmt |

**Page-initiated requests** (from `mgmt.js` / `portal.js`): all of the above plus `GET /i18n/<lang>.json` (portal only — see R8). OTA upload uses `XMLHttpRequest` POST with progress events.

## R5 — Scenario selection (clarified: startup + runtime)

- **Decision**: A scenario dictionary held in emulator state:
  `join_outcome` (`success`|`auth`|`not_found`), `ota_outcome` (`success`|`invalid_image`|`write_error`), `sensor_valid` (bool), `time_synced` (bool), `wifi_connected` (bool, drives mgmt status wifi block), `storage_free_kb` (int). Initial values from CLI (`--join-outcome auth`, `--sensor-invalid`, `--time-not-synced`, ...). Runtime: `GET /emu/scenario` returns current dict; `PUT /emu/scenario` merges a JSON patch; a dependency-free control page at `GET /emu` (mgmt port) renders checkboxes/selects for the same. Additionally, designated scan SSIDs (`Emu-WrongPass`, `Emu-NotFound`) force the corresponding join outcome regardless of the flag, so portal failure paths are testable straight from the UI.
- **Rationale**: Satisfies clarification "Both"; the `/emu/*` namespace cannot collide with device routes (device knows no such paths), keeping the pages 100 % device-compatible.
- **Alternatives considered**: Interactive stdin console (rejected: clunky, untestable); config file watch (rejected: slower loop than an HTTP toggle).

## R6 — Synthetic data generation

- **Decision**: Temperature = daily sinusoid (21 °C ± 4 °C) + small deterministic noise, seeded so runs are reproducible; regenerated continuously so `/api/status` varies over time (FR-008). History: generated at startup covering `--history-hours` (default 24) at 60 s intervals, appended to while running; served with `from`/`to` filtering exactly like `history_query`. Scan list: fixed set of ~8 networks with varied RSSI/security including the designated outcome SSIDs. Timezone catalog: hardcoded representative list (~25 zones incl. `UTC`, common `Europe/…`, `America/…`, `Asia/…`) used both for `/api/timezones` and for validating `unknown_timezone` on `PUT /api/config`.
- **Rationale**: Deterministic data keeps visual tests repeatable; 24 h @ 60 s ≈ 1440 points renders a realistic chart density. The firmware's real tz list lives in `tz_table.c` (another component); duplicating a representative subset avoids parsing C at runtime while preserving the validation behavior the page must handle.
- **Alternatives considered**: Parsing `tz_table.c` at startup for the exact list (rejected: brittle coupling for marginal benefit; revisit if the page ever hardcodes zone expectations); random non-seeded data (rejected: flaky-looking charts).

## R7 — Join & OTA state machines

- **Decision**: Join: `POST /api/wifi` validates like firmware, sets `connecting`, and a timer (~2 s) resolves to `connected` (with 4-hex-char suffix) or `failed` with `reason` per scenario/designated SSID. OTA: `POST /api/ota` streams the body, updating `progress_pct` as bytes arrive (device-identical), enforces the 3 MB / 409 / 507 rules, then `validating` → outcome per scenario. On success: respond `{"status":"applied","reboot_in_s":3}`, hold `applied_pending_reboot` for 3 s, then simulate reboot: uptime resets to 0 and OTA state returns to `idle`.
- **Rationale**: Matches the exact state strings and transitions the pages poll for; the simulated reboot exercises the page's post-update behavior (device actually drops the connection — emulator staying up is a documented fidelity deviation that favors testability).
- **Alternatives considered**: Instant transitions (rejected: pages show progress/spinner states that need nonzero duration to be seen and tested).

## R8 — i18n behavior (firmware gap found)

- **Decision**: Emulator ports `accept_language_pick()` (en/de/fr/uk, q-values, primary-subtag match, `en` fallback — verified against `test_i18n.c` cases, which become the Python unit tests), serves `/i18n/<lang>.json` from `www/i18n/` on **both** ports, sets `Content-Language`, and injects `data-lang="<lang>"` into the `<html>` tag of portal `index.html` while serving it.
- **Finding (gap)**: `portal.js` fetches `/i18n/${lang}.json` and reads `document.documentElement.dataset.lang`, and `CMakeLists.txt` embeds the JSON files — but **no firmware route serves `/i18n/*` and nothing injects `data-lang`**, so on real hardware translations silently never load (JS falls back to English). The comment in `portal_server.c:65` documents the injection intent. The emulator implements the intent so SC-005 (verify all languages) is achievable; the firmware gap should be fixed in a separate firmware change.
- **Alternatives considered**: Byte-faithful emulation (404 on `/i18n/*`, no injection) — rejected because it would make SC-005 unsatisfiable and merely reproduce a bug; the deviation is confined, documented here and in the contract.

## R9 — Asset serving & caching

- **Decision**: Serve `mgmt/`, `portal/`, `i18n/` files from `components/web_server/www/` resolved relative to the script location; open + read per request (no caching); `Cache-Control: no-cache` on **all** emulator responses; correct MIME types (`text/html; charset=utf-8`, `text/css`, `application/javascript`, `application/json`). If the `www` root is missing, exit at startup naming the expected absolute path (spec edge case). Unknown routes → 404 plus a console log line (spec edge case: visible emulation gaps).
- **Rationale**: FR-003 (edit → refresh) requires defeating browser caching; the device's `max-age=3600` on css/js would break the core loop. This is a deliberate, documented deviation. Firmware serves gzip-compressed embedded copies; the emulator serves identical *content* uncompressed, which browsers treat identically.
- **Alternatives considered**: Honoring device cache headers (rejected: breaks FR-003); auto-reload via SSE/websocket injection (rejected: would modify served pages and add complexity — manual refresh is within SC-002's 10 s budget).

## R10 — Startup & browser open

- **Decision**: `argparse` CLI; start both servers in threads; when both sockets are bound, print both URLs plus the `/emu` control URL, then `webbrowser.open()` the mgmt page and portal page (FR-014). `--no-browser` suppresses opening (useful for CI/scripted runs). Ctrl-C shuts down both servers cleanly.
- **Rationale**: `webbrowser` is the stdlib, cross-platform way to hit the user's default browser. The opt-out is a trivial flag that keeps automated use possible without violating the auto-open directive for the normal path.
- **Alternatives considered**: `os.startfile`/`open`/`xdg-open` directly (rejected: `webbrowser` already abstracts this).

## R11 — Testing approach

- **Decision**: `tools/test_hw_emulator.py` using stdlib `unittest`: language-pick cases mirrored from `test_i18n.c`, join/OTA state-machine transitions (with injectable clock), synthetic generator properties (range, ordering, from/to filtering), and config validation errors. End-to-end validation is manual via `quickstart.md` (start emulator, walk both pages) — appropriate for a visual dev tool.
- **Rationale**: Pure-logic tests need no HTTP server and run in milliseconds; browser-level automation (Playwright etc.) would drag in exactly the kind of dependency the user excluded.
