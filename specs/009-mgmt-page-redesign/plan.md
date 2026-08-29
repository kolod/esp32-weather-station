# Implementation Plan: Management Page Redesign & Live Readings

**Branch**: `009-mgmt-page-redesign` | **Date**: 2026-08-29 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/009-mgmt-page-redesign/spec.md`

## Summary

Rework the `/mgmt` single page: (1) render current readings as a 2×2 grid (time,
temperature, pressure, humidity) with time-source and WiFi status as full-width rows
below; (2) push readings to the browser over a secured WebSocket (`wss://<host>/api/ws`)
so values refresh within ~1 s of a sensor sample instead of on a 5 s client poll, with
automatic reconnect and a poll fallback; (3) replace the always-visible history table
with a canvas line plot plus a Day/Week/Month/All period selector, move the CSV link to
the bottom of the card, and drop the "Load last 100 records" button; (4) constrain the
header so the caption lines up with the left edge of card content and the firmware
version with the right edge; (5) remove the boot-log download button (inline view only).

Technical approach: enable `CONFIG_HTTPD_WS_SUPPORT`, add a WebSocket handler and a
small broadcaster in the `web_server` component that subscribes to the existing
`APP_EVENT` events and sends the same JSON payload as `GET /api/status` to every
connected client via `httpd_queue_work`. The status-JSON builder is refactored into one
shared function. All plotting is hand-rolled `<canvas>` drawing in a new
`www/common/chart.js` — no external library (CSP `default-src 'self'`, Principle IV).

## Technical Context

**Language/Version**: C (C17) for firmware under ESP-IDF v6.0.2; vanilla ES2020
JavaScript + CSS for the page (no build step, no framework).

**Primary Dependencies**: `esp_http_server` / `esp_https_server` (WebSocket support),
`esp_event` (`APP_EVENT`), existing `history`, `settings`, `app_ctx`, `rtc_time`,
`boot_log` components. Browser: native `WebSocket` and `<canvas>` APIs.

**Storage**: No new persistent storage. History continues to be read via
`history_query()` / `GET /api/history`. Readings pushed over WS are RAM-only snapshots
of `app_state` (no per-sample flash writes — Principle IV).

**Testing**: `idf.py build` (Principle III); `tools/check_i18n.py` for translation
parity; existing host tests under `components/web_server/test/` (i18n) still pass;
manual validation via `quickstart.md` against a device or the hardware emulator
(spec 003).

**Target Platform**: ESP32-D0WDQ6, ST7789 TFT, BMP280/BME280 sensor; management page
served over HTTPS to any LAN browser (desktop + mobile).

**Project Type**: Embedded firmware with an embedded single-page web UI (assets
compiled into the image via `EMBED_FILES`).

**Performance Goals**: On-screen readings update < 1 s after a sensor sample in ≥ 95 %
of updates (SC-002); plot redraw < 2 s per period change (SC-005); "All" view stays
responsive at maximum stored history via client-side down-sampling (SC-006); no
horizontal page scroll at 360 px width (SC-009).

**Constraints**: 520 KB SRAM, no PSRAM. Each TLS socket is expensive (mbedTLS
buffers), so concurrent WS clients are capped (≤ 4 assumed, spec). WebSocket must be
WSS on the existing `:443` server — no plaintext downgrade (Principle V). No external
CDN / third-party JS (Principle IV, CSP `default-src 'self'`). Zero compiler warnings
(Principle III).

**Scale/Scope**: One device, a handful of concurrent viewers on a home LAN. One HTML
file, one CSS file, two JS files (`mgmt.js`, new `chart.js`), four i18n packs, one new
C source pair (`ws_broadcast.c/.h`), edits to `mgmt_server.c` / `handlers_mgmt.c` /
`sdkconfig.defaults`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Assessment |
|-----------|------------|
| **I. ESP-IDF Component Architecture** | PASS — all firmware changes stay in `web_server`; the broadcaster talks to the rest of the system only through `app_ctx` (`app_state` + `APP_EVENT`). No new component; WebSocket is a web-server concern. Public surface is a single `ws_broadcast_start(httpd_handle_t)` in the component's headers. |
| **II. Hardware Abstraction Layer** | PASS — no peripheral, GPIO, or bus code touched. |
| **III. Build Integrity (NON-NEGOTIABLE)** | PASS with action — `sdkconfig.defaults` changes (`CONFIG_HTTPD_WS_SUPPORT=y`, socket count, optional mbedTLS buffer tuning) require a verified clean rebuild; no warnings to be introduced. Toolchain remains ESP-IDF v6.0.2 / `esp32`. |
| **IV. Embedded Resource Discipline** | PASS with justification — raising `httpd.max_open_sockets` (2 → 5) increases mbedTLS heap use; bounded and justified in Complexity Tracking. WS client FD list is a fixed-size static array. Broadcast payload uses a single stack/static buffer reused from the status builder. No new tasks (async sends run on the httpd worker via `httpd_queue_work`); the event handler only enqueues. No flash writes added. |
| **V. Network & Security Standards** | PASS — WebSocket endpoint is registered only on the HTTPS server (`wss://`), so it inherits the device cert and the HTTP→HTTPS rejection. CSP unchanged: `connect-src` inherits `default-src 'self'`, which permits a same-origin `wss://`. No secrets in payloads or logs. |

**Result**: PASS. One justified Principle IV deviation (socket count) tracked below.

## Project Structure

### Documentation (this feature)

```text
specs/009-mgmt-page-redesign/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   ├── ws-readings.md    # WebSocket /api/ws message contract
│   └── history-api.md    # GET /api/history period-query usage (existing endpoint)
├── checklists/
│   └── requirements.md   # Spec quality checklist (already created)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
components/web_server/
├── CMakeLists.txt                 # + www/common/chart.js in EMBED_FILES; + ws_broadcast.c
├── mgmt_server.c                  # raise httpd.max_open_sockets; call ws_broadcast_start()
├── handlers_mgmt.c                # register GET /api/ws; extract build_status_json()
├── handlers_mgmt.h                # (unchanged public surface)
├── ws_broadcast.c                 # NEW — APP_EVENT subscriber → async send to WS clients
├── ws_broadcast.h                 # NEW — ws_broadcast_start(httpd_handle_t)
├── status_json.c / status_json.h  # NEW (or static in handlers_mgmt.c) — shared JSON builder
└── www/
    ├── mgmt/
    │   ├── mgmt.html             # 2×2 readings grid; header wrapper; history=plot+selector;
    │   │                         #   CSV link at card bottom; remove btn-load-hist & btn-bootlog
    │   ├── mgmt.css              # grid layout; header alignment; plot/selector styles
    │   └── mgmt.js               # WebSocket client + reconnect/backoff + poll fallback;
    │                             #   period selector wiring; remove load-hist / bootlog-btn logic
    ├── common/
    │   └── chart.js             # NEW — minimal canvas time-series renderer
    └── i18n/
        ├── en.json  de.json  fr.json  uk.json   # + period/plot/empty-state keys; drop obsolete

sdkconfig.defaults                 # CONFIG_HTTPD_WS_SUPPORT=y; (+ socket / mbedTLS tuning)

tools/check_i18n.py                # run to verify pack parity after i18n edits
```

**Structure Decision**: Embedded firmware + embedded SPA. No new ESP-IDF component —
the WebSocket broadcaster is intrinsically a `web_server` responsibility and
communicates outward only through `app_ctx`. Client-side charting is a standalone
`www/common/chart.js` so both the portal and future pages could reuse it, mirroring the
existing `www/common/i18n.js` pattern.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| Raise `httpd.max_open_sockets` from 2 to 5 (more concurrent mbedTLS sessions ⇒ more heap) | A WebSocket holds a socket open for the life of the page; with the current cap of 2, one open page plus any second request (asset fetch, CSV download, a second viewer) exhausts the pool and the server stalls. Need headroom for ~3 live pages + 1 transient request. | Keeping the cap at 2 makes the feature unusable with more than one client. Using a second plaintext HTTP server for WS violates Principle V (HTTPS-only management). Server-Sent Events would still consume a socket per client — same pressure — and the spec explicitly asks for a WebSocket. Heap impact is bounded by tuning mbedTLS content-length / enabling dynamic buffers (see research.md) and by the hard cap itself. |
