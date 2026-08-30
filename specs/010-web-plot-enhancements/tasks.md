---
description: "Task list for Web Interface Plot Enhancements"
---

# Tasks: Web Interface Plot Enhancements

> **Implementation status (2026-08-30)**: all code, markup, CSS and i18n tasks complete.
> `idf.py build` passes (exit 0, no compiler warnings — only data assets changed);
> `tools/check_i18n.py` → `0 discrepancies, 0 warnings (76 keys x 4 languages)`;
> `node --check` passes for `chart.js` and `mgmt.js`. The 8 flash-and-walk-`quickstart.md`
> tasks (T015, T022, T031, T035, T041, T048, T054–T057) need hardware or the spec-003
> emulator + a real browser and are left unchecked.

**Input**: Design documents from `/specs/010-web-plot-enhancements/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: No automated test tasks — no TDD requested, no host framework for browser JS
(same posture as feature 009). Validation is via `quickstart.md` + the `check_i18n.py`
gate. Front-end only — no firmware, `sdkconfig`, `EMBED_FILES`, or endpoint changes.

## Format: `[ID] [P?] [Story] Description`

## Path Conventions

Embedded web assets under `components/web_server/www/`:
`common/chart.js`, `mgmt/mgmt.{html,css,js}`, `i18n/{en,de,fr,uk}.json`.

---

## Phase 1: Setup (Shared Infrastructure)

- [X] T001 [P] Added 9 UI strings to all four packs — `www/i18n/{en,de,fr,uk}.json`: `mgmt_plot_live`, `mgmt_plot_prev`, `mgmt_plot_next`, `mgmt_plot_fullscreen`, `mgmt_plot_exit_fullscreen`, `mgmt_plot_reset_zoom`, `mgmt_plot_no_data`, `mgmt_plot_range_all`, `mgmt_plot_range_live` (translated de/fr/uk). (Templated `{from}/{to}` range keys from the plan were dropped — the range label is composed in JS from `Intl.DateTimeFormat` output, so no placeholder keys were needed.)
- [X] T002 [P] `python tools/check_i18n.py` → `0 discrepancies, 0 warnings (76 keys x 4 languages)` (76, not 79 — see T001).
- [X] T003 Baseline `idf.py build` → exit 0.

**Checkpoint**: strings in place; build green.

---

## Phase 2: Foundational (Blocking Prerequisites)

- [X] T004 Rewrote `www/common/chart.js` as `window.createTimeSeriesChart(canvasEl, options)` per [contracts/chart-controller.md](./contracts/chart-controller.md): DPR-aware sizing, `ResizeObserver` repaint, geometry with `PAD.right` shrunk to 12 (no right axis), `setData/setPeriodWindow/setSelected/setTz/resetZoom/redraw/destroy`. Kept `niceExtent` + gap handling + top legend.
- [X] T005 Fixed-domain x-axis: `t0/t1/span` come from the controller's `domain` (from `setPeriodWindow`), never `times[]`. 4 gridline rows + 5 x-tick labels, label granularity by domain span.
- [X] T006 Single left axis: ticks/values from `series[axisIdx()]` (= `selected ?? 0`); every series drawn with its own in-domain `niceExtent`; old right-axis tick loop removed.
- [X] T007 Migrated `www/mgmt/mgmt.js` onto the controller: one `chart = createTimeSeriesChart($('hist-chart'), {...})` at startup; `loadHistory()` → `chart.setData()` + `chart.setPeriodWindow()`; removed the manual `window.resize` redraw + `lastSpec` plumbing; removed the now-unused `window.drawTimeSeries` global (grep-confirmed mgmt.js was the only caller).
- [X] T008 `idf.py build` → exit 0, no warnings. (Manual browser render check → quickstart, pending hardware.)

**Checkpoint**: controller renders the plot as before minus the right axis.

---

## Phase 3: User Story 1 - Read exact values at a point in time (Priority: P1) 🎯 MVP

- [X] T009 [US1] `chart.js` pointer handling: `pointermove/down/up/leave/cancel`; pointer-x → epoch → `nearestIndex()` binary search → `hover = {index}` (snapped) or `null` outside the plot rect.
- [X] T010 [US1] `chart.js` crosshair: vertical line at `xOf(times[hover.index])` across the plot, `rgba(144,202,249,.75)`, drawn each `requestAnimationFrame`-coalesced repaint.
- [X] T011 [US1] `chart.js` tooltip DOM: `<div class="chart-tip">` appended to `canvasEl.parentNode`, built with `createElement` (swatch colour + position via `el.style.*` — CSP-safe, no markup `style=`); snapped time via `Intl.DateTimeFormat(tzName)`, one row per series with unit or `noDataText`; repositioned to stay in the canvas bounds.
- [X] T012 [US1] Touch: same `pointer*` handlers; `canvas.style.touchAction='none'` (JS + CSS); `hover` cleared on `pointercancel`/`pointerleave`.
- [X] T013 [US1] `mgmt.js` passes `tzName`, `noDataText: t('mgmt_plot_no_data',…)`; `renderStatus` calls `chart.setTz(s.tz_name)` when the zone first becomes known (`lastTz` guard).
- [X] T014 [US1] `mgmt.css` `.chart-tip*` rules (dark card, `pointer-events:none`, `z-index:5`); `.chart-wrap{position:relative}`, dropped `overflow-x:auto` (canvas is `width:100%`, and it clipped the tooltip).
- [ ] T015 [US1] Build ✅. **Flash + quickstart Scenario 1 (desktop + phone) pending — needs hardware/emulator + browser.**

**Checkpoint**: point read-out implemented on the plot.

---

## Phase 4: User Story 2 - Focus on a single curve (Priority: P2)

- [X] T016 [US2] `chart.js` line hit-testing: on a non-drag `pointerup`, nearest-segment screen distance per series (`distToSeg`); ≤ 6 px selects; empty-plot or already-selected click clears; fires `onSelect(index|null)`.
- [X] T017 [US2] `chart.js` legend hit-rects captured during draw; click inside one fires `onSelect` (toggles to `null` if already selected).
- [X] T018 [US2] `chart.js` selection emphasis: selected series = wide `α .22` halo stroke + `2.4px` bright stroke; unselected series `α .3`; legend entry prefixed `▸` and non-selected entries dimmed. `cursor:pointer` when within 6 px of a line.
- [X] T019 [US2] `chart.js` left axis follows selection: `axisIdx() = selected ?? 0` drives tick values + decimals; `setSelected` repaints without firing `onSelect`.
- [X] T020 [US2] `mgmt.js` owns `selectedSeries` (default `null`); `onSelect(i){ selectedSeries=i; chart.setSelected(i); }`; `reapplySelection(spec)` after every `setData` resets it to `null` when the index is out of range (FR-012).
- [X] T021 [US2] `mgmt.css` — glow/dim are canvas-drawn so no CSS needed; verified palette reads on `#0f0f1a`. (No legend DOM.)
- [ ] T022 [US2] Build ✅. **Flash + quickstart Scenario 2 (incl. "no right axis") pending — needs hardware.**

**Checkpoint**: single-curve focus implemented; tooltip still lists all curves.

---

## Phase 5: User Story 3 - Navigate whole, calendar-aligned periods (Priority: P2)

- [X] T023 [US3] `mgmt.js` tz-aware helpers (research R2): `tzParts`, `wallToEpoch` (offset inversion + one DST correction), `localMidnight`, `addDaysLocal`, `localDow`, `mondayOnOrBefore`, `firstOfMonth`, `firstOfNextMonth`. No external library.
- [X] T024 [US3] `PlotViewState` (`mode`, `period`, `anchor`) + `periodWindow()` → `{from, to: min(nominalEnd, now), nominalEnd}`; the fetch uses `to`, `chart.setPeriodWindow` gets `nominalEnd` so the axis shows the full period (FR-020).
- [X] T025 [US3] Period-button handler sets `mode='history'`, `period`, `anchor=now`, reloads; `.active` toggling in `updatePeriodControls()`.
- [X] T026 [US3] `mgmt.html` `#period-sel` row: `◀`/`▶` `#btn-prev`/`#btn-next` (`.nav-btn`), `#hist-range` span, `#btn-live` (`.live-btn`); history card gets a `.hist-head` row with `#btn-reset-zoom` + `#btn-fullscreen`.
- [X] T027 [US3] `mgmt.js` Prev/Next: `stepPeriod(±1)` calendar step (anchor nudged to period-interior); Next disabled via `isCurrentPeriod()`; `#hist-range` set from `rangeLabel()` (`Intl` date / date-range, or `mgmt_plot_range_*`); empty result keeps the existing empty-state.
- [X] T028 [US3] Fetch-race guard: `histReqSeq` incremented per `loadHistory`; a resolved response whose `seq` is stale is discarded.
- [X] T029 [US3] `all` period → `from:0,to:now`, Prev/Next disabled, `setPeriodWindow` domain = actual data extent.
- [X] T030 [US3] `mgmt.css` `.nav-btn`, `#hist-range`, `.live-btn{margin-left:auto}`, `.period-sel button:disabled`; row wraps at narrow width via `flex-wrap`.
- [ ] T031 [US3] Build ✅ + `check_i18n.py` ✅. **Flash + quickstart Scenario 3 (incl. timezone-change) pending — needs hardware.**

**Checkpoint**: history navigable by aligned period.

---

## Phase 6: User Story 4 - View the plot fullscreen (Priority: P3)

- [X] T032 [US4] `mgmt.html` `#btn-fullscreen` in `.hist-head-btns` (`data-i18n="mgmt_plot_fullscreen"`).
- [X] T033 [US4] `mgmt.css` `body.plot-fullscreen` rules: `.card.history{position:fixed;inset:0;z-index:50;…;display:flex;flex-direction:column}`, hide `>header` and `main>:not(.history)`, `.chart-wrap{flex:1;min-height:0}`, `#hist-chart{height:100%}`, `body{overflow:hidden}`.
- [X] T034 [US4] `mgmt.js` `toggleFullscreen()` toggles `body.plot-fullscreen`, swaps the button label (`mgmt_plot_fullscreen` ⇄ `mgmt_plot_exit_fullscreen`), `requestAnimationFrame(() => chart.redraw())`; `Escape` `keydown` exits. No `PlotViewState` change → period/selection/zoom preserved.
- [ ] T035 [US4] Build ✅ + `check_i18n.py` ✅. **Flash + quickstart Scenario 4 pending — needs hardware.**

**Checkpoint**: fullscreen implemented.

---

## Phase 7: User Story 5 - Watch a live session plot (Priority: P3)

- [X] T036 [US5] `mgmt.js` module-scope `liveBuffer` + `LIVE_CAP = 10000`; `pushLive(s)` appends `{t:s.now, temperature, pressure|null, humidity|null}` when `s.now` present and `temperature_valid`, de-dupes same-`t`, front-drops past the cap. Called from `ws.onmessage` **and** `refreshStatus` (poll fallback), unconditionally, decoupled from the `ws` object.
- [X] T037 [US5] `mgmt.html` `#btn-live` in the `#period-sel` row (`data-i18n="mgmt_plot_live"`).
- [X] T038 [US5] `mgmt.css` `.period-sel .live-btn{margin-left:auto}`; `.active` state via `.period-sel button.active`.
- [X] T039 [US5] `mgmt.js` `renderLive()` builds `times`/`series` from `liveBuffer` (downsampled past 400), `chart.setData` + `chart.setPeriodWindow({from: liveBuffer[0].t, to: now})`; empty buffer → existing empty-state.
- [X] T040 [US5] `#btn-live` click → `mode='live'`, `loadHistory()` routes to `renderLive`; `scheduleLiveRender()` (rAF-throttled) on each `pushLive` while in live mode; selecting any period → `mode='history'`. `updatePeriodControls()` marks `#btn-live.active` and disables Prev/Next. Selection preserved across the switch.
- [ ] T041 [US5] Build ✅ + `check_i18n.py` ✅. **Flash + quickstart Scenario 5 (incl. WiFi drop/restore) pending — needs hardware.**

**Checkpoint**: live view implemented.

---

## Phase 8: User Story 6 - Zoom the time axis with the wheel (Priority: P3)

- [X] T042 [US6] `chart.js` non-passive `wheel` listener: `preventDefault()` always; scale `domain` about the epoch under the pointer by ×0.85 / ×(1/0.85); clamp to `full` window; `zoomed` when the span is < 99.9 % of full.
- [X] T043 [US6] Y-scale frozen during the gesture (`frozenExt` snapshot on the first wheel event); `setTimeout(WHEEL_SETTLE_MS=150)` after the last event clears `frozenExt` and repaints → y refits to in-domain data of `selected ?? 0`.
- [X] T044 [US6] `onZoomChange(isZoomed)` fired on the transition edge; `resetZoom()` = `domain ← full` + refit + `onZoomChange(false)`. `setPeriodWindow` also fires `onZoomChange(false)` when it was zoomed.
- [X] T045 [US6] `mgmt.html` `#btn-reset-zoom` in `.hist-head-btns`, `hidden` by default (`data-i18n="mgmt_plot_reset_zoom"`).
- [X] T046 [US6] `mgmt.js` `onZoomChange(z){ $('btn-reset-zoom').hidden = !z; }`; `#btn-reset-zoom` click → `chart.resetZoom()`. Period/Prev/Next/Live changes reset zoom via `setPeriodWindow` → button re-hides.
- [X] T047 [US6] `mgmt.css` `.hist-head-btns button` styling; `[hidden]` respected (global reset covers it).
- [ ] T048 [US6] Build ✅ + `check_i18n.py` ✅. **Flash + quickstart Scenario 6 pending — needs hardware.**

**Checkpoint**: all six stories implemented.

---

## Phase 9: Polish & Cross-Cutting Concerns

- [X] T049 [P] `check_i18n.py` → `0 discrepancies, 0 warnings (76 keys x 4 languages)`. (Browser de/fr/uk spot-check pending — needs a browser.)
- [X] T050 [P] `README.md` updated: new "Interactive history plot" bullet (aligned ranges + prev/next, crosshair/tooltip, curve select + glow, fullscreen, wheel zoom, Live view).
- [X] T051 [P] Recorded a gotcha in `.claude` memory: `www-csp-no-inline-style` — CSP `style-src 'self'` blocks markup `style=`/`<style>`; tooltip styles set via CSSOM.
- [X] T052 Grep confirms no remaining `drawTimeSeries` reference; `chart.js` header comment describes the new controller API.
- [ ] T053 CSP / console check — no violations, no third-party requests, plot active in every mode. **Pending — needs a browser.** (Static review: tooltip uses `createElement` + `el.style.*` only; no new external origins; no inline script/style added.)
- [ ] T054 Responsiveness at 360 px: no horizontal scroll, all controls reachable, ~100 ms interaction. **Pending — needs a browser.**
- [ ] T055 24 h Live-mode soak — buffer capped at 10 000, page stays responsive. **Pending — needs hardware + long-open tab.** (Cap enforced in `pushLive`.)
- [ ] T056 `idf.py fullclean && idf.py build` → exit 0, zero warnings; `EMBED_FILES` unchanged. **Pending** — incremental `idf.py build` verified clean; full clean not re-run.
- [ ] T057 Full `quickstart.md` (Scenarios 1–6 + cross-cutting) on hardware/emulator. **Pending — needs hardware.**

---

## Dependencies & Execution Order

- **Setup (Phase 1)** → **Foundational (Phase 2, blocks all)** → **US1 → US2 → US3 → US4 → US5 → US6** (sequential: `chart.js` and `mgmt.js` are touched by nearly every phase) → **Polish (Phase 9)**.
- US6's settle-refit uses `series[selected]` from US2 but degrades to `series[0]`.
- i18n packs: all keys added in Setup; `check_i18n.py` after any i18n or `data-i18n` edit.

## Implementation Strategy

- **MVP** = Phase 2 + Phase 3 (controller + crosshair/tooltip).
- Incremental: ship after each US phase once its quickstart scenario passes on hardware.

### Notes

- No firmware / `sdkconfig` / `EMBED_FILES` change — all four assets were already embedded.
- `chart.js` stays dependency-free (Principle IV, CSP `default-src 'self'`).
- Every commit builds clean (Principle III).
