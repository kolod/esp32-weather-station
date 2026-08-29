---
description: "Task list for Management Page Redesign & Live Readings"
---

# Tasks: Management Page Redesign & Live Readings

> **Implementation status (2026-08-29)**: all code, config and i18n tasks complete.
> `idf.py build` passes with zero warnings; `tools/check_i18n.py` passes (67 keys × 4).
> The 7 flash-and-walk-`quickstart.md` tasks (T015, T022, T030, T035, T040, T043, T046)
> require hardware or the spec-003 emulator and are left unchecked.

**Input**: Design documents from `/specs/009-mgmt-page-redesign/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: No automated test tasks — the feature spec did not request TDD and there is
no host test framework for the browser JS. Validation is via `quickstart.md` scenarios
and the existing `tools/check_i18n.py` gate. The existing host test in
`components/web_server/test/` (i18n applier) must keep passing.

**Organization**: Tasks grouped by user story. `mgmt.html`, `mgmt.css`, and `mgmt.js`
are edited by several stories — tasks that touch the same one of those files are
sequential across stories (never `[P]` together), even though the stories are otherwise
independent.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: US1–US5 from spec.md

## Path Conventions

Firmware component: `components/web_server/`. Embedded web assets:
`components/web_server/www/`. Build config: `sdkconfig.defaults`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Enable WebSocket support, size the socket pool, and create the new
source/asset stubs so later phases only fill in bodies.

- [x] T001 Enabled WebSocket + mbedTLS dynamic buffers + LWIP socket headroom. `sdkconfig.defaults`: `CONFIG_HTTPD_WS_SUPPORT=y`, `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, `CONFIG_LWIP_MAX_SOCKETS=16`. The generated (gitignored) `sdkconfig` had a stale `# CONFIG_HTTPD_WS_SUPPORT is not set` that overrode the default — fixed in place too; a `fullclean` build regenerates it from defaults.
- [x] T002 `components/web_server/mgmt_server.c`: `cfg.httpd.max_open_sockets` 2 → 5 with a comment pointing at spec 009 Complexity Tracking.
- [x] T003 [P] Created `components/web_server/ws_broadcast.h` — `ws_broadcast_start/stop`, `ws_broadcast_add_client/remove_client`.
- [x] T004 [P] Created `components/web_server/ws_broadcast.c` (full implementation, see T016/T017).
- [x] T005 [P] Created `components/web_server/www/common/chart.js` (full implementation, see T023).
- [x] T006 `components/web_server/CMakeLists.txt`: `ws_broadcast.c` in `SRCS`, `www/common/chart.js` in `EMBED_FILES`.
- [x] T007 Baseline build: `idf.py build` → exit 0, no compiler warnings.

**Checkpoint**: WS support compiled in, socket pool sized, stubs wired into the build.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Single source of truth for the status payload — required by User Story 2
(the WS frame must be byte-identical to `GET /api/status`).

**⚠️ CRITICAL**: T008–T009 block User Story 2.

- [x] T008 `build_status_json(char *buf, size_t buf_len)` extracted in `handlers_mgmt.c`, declared in `handlers_mgmt.h`. Same `snprintf` block, same field order/format; returns `ESP_ERR_INVALID_ARG` if `buf_len < 1152`.
- [x] T009 `api_status()` is now a thin wrapper over `build_status_json()` (local `char buf[1152]` → `httpd_resp_sendstr`). Payload string is byte-identical to before (same format spec).

**Checkpoint**: `build_status_json()` reusable; `/api/status` behavior identical.

---

## Phase 3: User Story 1 - Read current conditions at a glance (Priority: P1) 🎯 MVP

**Goal**: Current Readings card renders as a 2×2 grid — time (top-left), temperature
(top-right), pressure (bottom-left), humidity (bottom-right) — with time-source and
WiFi-status as full-width rows below, and correct placeholders when a value/sensor is
absent.

**Independent Test**: Load `https://<device>/` with a BME280 → all four quarters
populated in the right positions; swap to BMP280 → humidity quarter shows `---`;
narrow to 360 px → no horizontal scroll, grid collapses to one column
(quickstart.md Scenario A).

### Implementation for User Story 1

- [x] T010 [US1] `mgmt.html` readings card is now `.readings-grid` with four `.quarter` blocks (time / temperature / pressure / humidity), each a `.q-label` + `.q-value`. `#time-sync` sits inside the time quarter; `#time-source` and `#wifi-status` are full-width rows after the grid.
- [x] T011 [US1] Added `mgmt_label_time` + `mgmt_label_temperature` to all four packs (`mgmt_label_pressure`/`mgmt_label_humidity` reused).
- [x] T012 [US1] `mgmt.css`: `.readings-grid` 2-col grid, `.quarter`/`.q-label`/`.q-value` typography, `@media (max-width:460px)` → single column. Dead `.reading-row`/`.time-row` rules removed.
- [x] T013 [US1] `renderStatus(s)` writes each quarter with placeholders; pressure `---` unless sensor is bmp280/bme280 and valid; humidity `---` unless bme280 and valid. `hidden` toggling on pressure/humidity rows removed.
- [x] T014 [US1] `python tools/check_i18n.py` → `0 discrepancies, 0 warnings (67 keys x 4 languages)`.
- [ ] T015 [US1] Build ✅ (zero warnings). **Flash + quickstart Scenario A pending — needs hardware/emulator.**

**Checkpoint**: Readings card is the new 2×2 layout, still fed by the 5 s poll — a shippable MVP.

---

## Phase 4: User Story 2 - Live-updating readings (Priority: P1)

**Goal**: While the page is open, readings refresh within ~1 s of a sensor sample over
`wss://<host>/api/ws`, with automatic backoff reconnect and a poll fallback when the
socket is down.

**Independent Test**: Open the page → DevTools shows an open WS; temperature changes
within ~1 s of each sample with no reload; kill WiFi → reconnect attempts back off and
polling resumes; restore → WS re-establishes within ~15 s (quickstart.md Scenarios
B/C/D). Depends on Phase 2 (`build_status_json`).

### Implementation for User Story 2

- [x] T016 [US2] `ws_broadcast.c` client registry: `ws_broadcast_add_client` (dedupe, reject at `WS_MAX_CLIENTS`=4), `ws_broadcast_remove_client`, both under a mutex.
- [x] T017 [US2] Broadcast path: one `on_app_event` handler for the five `APP_EVENT` ids enqueues a single `httpd_queue_work(ws_broadcast_work)`; `ws_broadcast_work` (httpd task) builds the JSON once into a heap buffer and `httpd_ws_send_frame_async`s it to each fd, pruning on `HTTPD_WS_CLIENT_WEBSOCKET` mismatch or send failure. `ws_broadcast_start`/`stop` register/unregister the handler.
- [x] T018 [US2] `/api/ws` handler in `handlers_mgmt.c` (`.is_websocket = true`): registers the sockfd on handshake, CLOSE-frames + drops when the registry is full, drains and ignores client frames, removes fd on CLOSE. Registered in `register_mgmt_handlers()` (loop bound 8 → 9, plus a separate `is_websocket` registration).
- [x] T019 [US2] `mgmt_server.c` calls `ws_broadcast_start(s_server)` after `register_mgmt_handlers` and `ws_broadcast_stop()` in `mgmt_server_stop()`. `REQUIRES` already covered `app_ctx`/`esp_event`.
- [x] T020 [US2] `mgmt.js` `connectWs()` opens `wss://${location.host}/api/ws`; `onopen` stops the poll + resets backoff; `onmessage` → `renderStatus(JSON.parse)`; `onclose`/`onerror` → `scheduleReconnect()` (poll on, `setTimeout` with 1 s→30 s doubling + ±20 % jitter).
- [x] T021 [US2] Poll lifecycle: `startPolling`/`stopPolling` around a `pollTimer`; immediate `refreshStatus()` + `startPolling()` + `connectWs()` on load; poll runs only while the socket is not open.
- [ ] T022 [US2] Build ✅ (zero warnings). **Flash + quickstart Scenarios B/C/D pending — needs hardware.**

**Checkpoint**: Readings update live over WSS with graceful degradation; US1 layout unaffected.

---

## Phase 5: User Story 3 - Visualize history as a plot with period selection (Priority: P2)

**Goal**: History card shows a canvas line plot (temperature always; pressure/humidity
when present) with a Day (default) / Week / Month / All selector; empty periods show a
localized message; large ranges are down-sampled for a responsive draw.

**Independent Test**: Open the History card → plot of last 24 h, Day selected; switch
Week/Month/All → redraws < 2 s each; empty period → "no data" message; All with large
history → no freeze (quickstart.md Scenario E). Independent of US1/US2 but edits the
shared `mgmt.html`/`css`/`js`.

### Implementation for User Story 3

- [x] T023 [US3] `chart.js` `window.drawTimeSeries(canvas, {times, series})`: DPR-aware sizing, axes/gridlines/time labels, one polyline per series with independent auto-scaling (series[0] → left axis, series[1] → right axis), `null`/NaN drawn as gaps, top legend with colored swatches. Zero dependencies.
- [x] T024 [US3] `mgmt.html` history card: table removed; `.period-sel` with four `<button data-period>` (Day active), `.chart-wrap > canvas#hist-chart`, hidden `#hist-empty`. `#btn-load-hist` gone.
- [x] T025 [US3] `<script src="/chart.js">` added before `/mgmt.js`; served by a new `chart_js` handler in `handlers_mgmt.c` (extern `_binary_chart_js_*`, `application/javascript`, `Cache-Control: max-age=3600`), registered as `/chart.js` (mirrors `/i18n.js` at root rather than `/common/`).
- [x] T026 [US3] Added `mgmt_period_{day,week,month,all}`, `mgmt_legend_{temperature,pressure,humidity}`, `mgmt_history_empty` to all four packs; `mgmt_heading_history` value → "History" / "Verlauf" / "Historique" / "Історія".
- [x] T027 [US3] `mgmt.js` `loadHistory(period)`: period→`from`, fetch `/api/history`, filter valid temps, empty → `showEmpty(true)`, else `downsample(recs, 400)` (time-bucketed mean, NaN-skipping) → build 1–3 series → `drawTimeSeries`. `#period-sel` click handler, debounced `resize` redraw, `loadHistory('day')` on load.
- [x] T028 [US3] `mgmt.css`: `.period-sel` button row + `.active`, `.chart-wrap{overflow-x:auto}`, `#hist-chart{width:100%;height:220px}`, `#hist-empty` muted, `.history-foot` flex row. Dead `.history table` rules removed.
- [x] T029 [US3] `check_i18n.py` → 0 discrepancies, 0 warnings.
- [ ] T030 [US3] Build ✅ (zero warnings). **Flash + quickstart Scenario E pending — needs hardware.**

**Checkpoint**: History is an interactive plot; CSV still where it was (moved in US4).

---

## Phase 6: User Story 4 - Export and trimmed history controls (Priority: P3)

**Goal**: "Download CSV" sits at the bottom of the history card; the "Load last 100
records" button no longer exists.

**Independent Test**: History card → CSV control is the last element; no load-history
button anywhere; CSV download is byte-identical to a pre-redesign export
(quickstart.md Scenario F). Small; edits shared `mgmt.html`/`css`/`js`/i18n.

### Implementation for User Story 4

- [x] T031 [US4] `#btn-csv` now lives in `.history-foot` at the bottom of the history card (after canvas + `#hist-empty`); `#btn-load-hist` removed.
- [x] T032 [US4] `mgmt.js` has no `btn-load-hist` reference; `#btn-csv` is a plain `<a download>`.
- [x] T033 [US4] `mgmt.css` `.history-foot` flex row places `#btn-csv` bottom-right; `#btn-load-hist` rule removed.
- [x] T034 [US4] `mgmt_btn_load_hist` removed from all four packs; `check_i18n.py` → 0 discrepancies, 0 warnings.
- [ ] T035 [US4] Build ✅. **Flash + quickstart Scenario F (incl. byte-identical CSV diff) pending — needs hardware.** CSV code path (`api_history_csv`) is untouched.

**Checkpoint**: History card controls finalized.

---

## Phase 7: User Story 5 - Cleaner header and boot-log card (Priority: P3)

**Goal**: Header caption aligns to the left edge of card content, firmware version to
the right edge; boot-log card has no download button (inline view only).

**Independent Test**: Desktop width → caption left edge and `#fw-version` right edge
line up with card text; 360 px → both visible, no overlap; boot-log card shows inline
text and no "Download boot.log" button; no-boot-log device → localized "not available",
still no button (quickstart.md Scenarios G/H).

### Implementation for User Story 5

- [x] T036 [US5] `mgmt.html` `<header>` now wraps `<h1>` + `#fw-version` in `<div class="header-inner">`; `#btn-bootlog` and its `.row` removed from the boot-log card (`<pre id="bootlog-content">` + `#bootlog-unavailable` kept).
- [x] T037 [US5] `mgmt.css`: `.header-inner{max-width:780px;margin:0 auto;padding:16px;display:flex;justify-content:space-between;flex-wrap:wrap}` (matches `main`'s column); bare `header` keeps only background/border; `#btn-bootlog` rule removed.
- [x] T038 [US5] `mgmt.js` boot-log `catch` no longer references `btn-bootlog`.
- [x] T039 [US5] `mgmt_btn_bootlog` removed from all four packs; `check_i18n.py` clean.
- [ ] T040 [US5] Build ✅. **Flash + quickstart Scenarios G/H pending — needs hardware.**

**Checkpoint**: All five stories independently functional.

---

## Phase 8: Polish & Cross-Cutting Concerns

- [x] T041 [P] Removed unused `mgmt_th_time/temperature/pressure/humidity` from all four packs; `check_i18n.py` → `0 discrepancies, 0 warnings (67 keys x 4 languages)`.
- [x] T042 [P] `README.md` API table lists `/api/ws`; adds a note on the 2×2 live-readings grid and the Day/Week/Month/All history plot.
- [ ] T043 Heap / socket validation (open 5 pages + CSV download, free heap > 40 KB, no `httpd` socket-exhaustion). **Pending — needs hardware/emulator.** Mitigations in place: `WS_MAX_CLIENTS`=4 cap, `max_open_sockets`=5, `CONFIG_LWIP_MAX_SOCKETS`=16, `CONFIG_MBEDTLS_DYNAMIC_BUFFER`=y.
- [~] T044 Regression: `idf.py build` ✅ (zero warnings); `tools/check_i18n.py` ✅; Configuration/OTA card code paths untouched (`api_config_put`, `handlers_ota.c` unchanged). The `components/web_server/test` component is a Unity test-component (no `project()`), not buildable standalone via `idf.py -C` in this repo — pre-existing, and `test_i18n.c` only covers `accept_language_pick`, unaffected by this feature.
- [x] T045 `idf.py fullclean && idf.py build` → exit 0. A follow-up incremental `idf.py build` (materialized `sdkconfig`) shows **zero CMake and zero compiler warnings**; the first post-`sdkconfig.defaults`-change build emits IDF's self-healing "Missing kconfig option — re-run" retry once (exit-10 rerun loop in `project.cmake`), which resolves within the same CMake invocation. `sdkconfig.defaults` diff limited to: `HTTPD_WS_SUPPORT=y`, `MBEDTLS_DYNAMIC_BUFFER=y`, `LWIP_MAX_SOCKETS=16`.
- [ ] T046 Full `quickstart.md` A–H on hardware. **Pending — needs hardware.**

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (Phase 1)**: no dependencies.
- **Foundational (Phase 2)**: after Setup. Blocks **US2 only** (`build_status_json`).
- **US1 (Phase 3)**: after Setup. Independent of Foundational.
- **US2 (Phase 4)**: after Foundational (T008–T009) + Setup (T001–T007).
- **US3 (Phase 5)**: after Setup. Independent of US1/US2 logic.
- **US4 (Phase 6)**: after US3 (edits the history card US3 rebuilt).
- **US5 (Phase 7)**: after Setup. Independent.
- **Polish (Phase 8)**: after all desired stories.

### Shared-file constraint

`mgmt.html`, `mgmt.css`, `mgmt.js` are touched by US1, US3, US4, US5. Do those stories'
file edits **sequentially** (recommended order US1 → US2 → US3 → US4 → US5). Firmware
files (`ws_broadcast.c`, `handlers_mgmt.c`, `mgmt_server.c`) are US2-only.
`chart.js` is US3-only. i18n packs are edited by US1/US3/US4/US5 — run
`check_i18n.py` after each.

### Within each story

Markup → styles → script wiring → i18n check → build/flash/validate.

### Parallel opportunities

- Setup: T003, T004, T005 in parallel (distinct new files); T006 after them; T007 last.
- Foundational: T008 then T009 (same file, sequential).
- US2: T016→T017 (same file) sequential; T018/T019 after; T020→T021 (mgmt.js) sequential.
- US3: T023 (`chart.js`) in parallel with T024 (`mgmt.html`); T025–T028 after.
- Polish: T041, T042 in parallel.

---

## Parallel Example: Setup

```bash
Task: "Create components/web_server/ws_broadcast.h"          # T003
Task: "Create components/web_server/ws_broadcast.c stub"     # T004
Task: "Create components/web_server/www/common/chart.js stub"# T005
```

## Parallel Example: User Story 3

```bash
Task: "Implement drawTimeSeries in www/common/chart.js"      # T023
Task: "Replace history card body in www/mgmt/mgmt.html"      # T024
```

---

## Implementation Strategy

### MVP scope

**User Story 1 only** (Phase 1 → Phase 3): the 2×2 readings layout, still fed by the
existing 5 s poll. Delivers the headline visual change and is fully shippable.

### Incremental delivery

1. Setup + Foundational → build green with stubs.
2. **US1** → 2×2 grid → validate Scenario A → ship (MVP).
3. **US2** → live WebSocket updates + fallback → validate B/C/D → ship.
4. **US3** → history plot + period selector → validate E → ship.
5. **US4** → CSV to bottom, drop load button → validate F → ship.
6. **US5** → header alignment + boot-log button removal → validate G/H → ship.
7. **Polish** → i18n cleanup, README, heap check, full clean build, full quickstart.

### Notes

- `[P]` = different files, no incomplete-task dependency.
- Commit after each task or logical group; every commit must build clean (Principle III).
- `python tools/check_i18n.py` must exit 0 before any build task in a story that
  touched i18n packs or `data-i18n` attributes.
- Do not add per-sample flash writes; the WS path is RAM-only (Principle IV).
