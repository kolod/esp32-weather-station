# Implementation Plan: Web Interface Plot Enhancements

**Branch**: `010-web-plot-enhancements` | **Date**: 2026-08-30 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/010-web-plot-enhancements/spec.md`

## Summary

Turn the static management-page history canvas into an interactive chart, entirely
client-side. Scope, in the spec's priority order:

1. **P1 — Crosshair + tooltip**: a vertical line follows the pointer; a tooltip
   reports the time at that position and each visible curve's value (or "no data").
2. **P2 — Curve selection**: click a line or its legend entry to select it; the
   selected curve glows, the others dim, and the (single) left y-axis switches to
   the selected curve's scale and unit. The **right y-axis is removed**.
3. **P2 — Calendar-aligned periods + navigation**: Day = 00:00–24:00, Week =
   first–last day, Month = day 1–last day, all in the device timezone; Previous /
   Next step one whole period; Next is capped at the current period; a date-range
   label shows what is on screen.
4. **P3 — Fullscreen**: expand the chart to fill the viewport, hide all other page
   sections, exit via a button or Escape.
5. **P3 — Live session plot**: a right-aligned "Live" button in the period row
   switches the chart to a bounded, in-memory buffer of readings received over the
   existing WebSocket since page load.
6. **P3 — Wheel zoom**: wheel over the plot zooms the time axis around the pointer;
   the y-axis holds during the gesture then re-fits to visible data; a "Reset"
   control appears whenever the view is zoomed.

**Technical approach**: replace the stateless `window.drawTimeSeries(canvas, spec)`
in `www/common/chart.js` with a small stateful controller (`createTimeSeriesChart`)
that owns the canvas, binds pointer/wheel/resize listeners, and holds view state
(period anchor, selection, zoom viewport, mode). `mgmt.js` keeps ownership of data
(history fetches, downsampling, the live buffer) and drives the controller. No new
files, no new firmware, no new HTTP endpoints — `GET /api/history` already accepts
arbitrary `from`/`to`, and the WebSocket already pushes timestamped readings. New
user-facing strings are added to the four i18n packs.

## Technical Context

**Language/Version**: Vanilla ES2020 JavaScript + CSS, no build step, no framework
(matches `mgmt.js` / `chart.js` / `i18n.js`). No firmware (C) changes.

**Primary Dependencies**: Browser `<canvas>` 2D, `PointerEvent`, `WheelEvent`,
`ResizeObserver`, `Intl.DateTimeFormat` (timezone-aware boundary math). Existing
`www/common/i18n.js` (`t` / `tn`). Existing endpoints `GET /api/history` and
`wss://<host>/api/ws` — both unchanged.

**Storage**: None. History is fetched per view; the live buffer is a bounded
in-RAM array in the page (dropped on reload). No `localStorage`, no flash writes.

**Testing**: `idf.py build` still clean (Principle III — only `EMBED_FILES` content
changes, and only if a file is added; here files are edited in place). `tools/check_i18n.py`
for translation-pack parity. Manual validation via `quickstart.md` against a device
or the hardware emulator (spec 003). Existing host tests under
`components/web_server/test/` are unaffected.

**Target Platform**: Management page served over HTTPS to any LAN browser, desktop
and mobile (phone-width supported). Modern evergreen browsers.

**Project Type**: Embedded firmware with an embedded single-page web UI; this
feature touches only the embedded web assets.

**Performance Goals**: Pointer/select/zoom/period/fullscreen feedback < 100 ms on a
mid-range phone (SC-008); redraw on interaction is a single `requestAnimationFrame`
canvas repaint of ≤ ~400 downsampled points per series; a 24 h open Live session
stays responsive via a hard cap on retained readings (SC-009).

**Constraints**: CSP `default-src 'self'` — no external scripts, styles, fonts, or
CDNs (Principle IV); all rendering hand-rolled on canvas. No horizontal page scroll
at 360 px width. WSS-only, inherited from the existing server (Principle V). Zero
new compiler warnings (Principle III).

**Scale/Scope**: One HTML file, one CSS file, two JS files (`mgmt.js`, `chart.js`),
four i18n packs. ~1 device, a handful of concurrent viewers. History up to the
full stored dataset, downsampled client-side to ≈ 400 points before drawing.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Assessment |
|-----------|------------|
| **I. ESP-IDF Component Architecture** | PASS — no firmware or component changes. All edits are in `web_server/www/`. `chart.js` stays a standalone reusable asset mirroring `www/common/i18n.js`. |
| **II. Hardware Abstraction Layer** | PASS — no peripheral, GPIO, or bus code touched. |
| **III. Build Integrity (NON-NEGOTIABLE)** | PASS — no `sdkconfig` change; `EMBED_FILES` list is unchanged (all four assets already embedded, edited in place). A verified `idf.py build` after the asset edits confirms the image still links. No warnings possible from data-only asset changes. |
| **IV. Embedded Resource Discipline** | PASS — no new flash writes, no new tasks, no heap pressure on the device. The only "resource" concern is browser memory for the Live buffer, bounded by a hard cap (see research.md R7). Asset size grows modestly (interaction code in `chart.js`, a few i18n keys); still well within the 16 MB flash and served uncompressed as today. |
| **V. Network & Security Standards** | PASS — no new endpoints; reuses the existing WSS push and the existing `GET /api/history`. No secrets involved. CSP unchanged and still satisfied (no new external origins). |

**Result**: PASS. No deviations — Complexity Tracking left empty.

**Post-Phase 1 re-check**: Still PASS. The design adds no firmware code, no new
files, no `EMBED_FILES` / `sdkconfig` changes, no endpoints, and no device-side
memory or flash cost. The only new bounded resource is the browser-side Live buffer
(cap 10 000 entries, research R7). CSP and WSS-only posture are unchanged.

## Project Structure

### Documentation (this feature)

```text
specs/010-web-plot-enhancements/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output — client-side view-state model
├── quickstart.md        # Phase 1 output — manual validation scenarios
├── contracts/
│   ├── chart-controller.md   # UI contract: createTimeSeriesChart() public API
│   └── history-api.md        # GET /api/history usage for aligned/past periods (endpoint unchanged)
├── checklists/
│   └── requirements.md       # Spec quality checklist (already created)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
components/web_server/
├── CMakeLists.txt                 # unchanged (all four assets already in EMBED_FILES)
└── www/
    ├── common/
    │   └── chart.js              # REWRITE — stateful createTimeSeriesChart():
    │                             #   fixed time domain, crosshair+tooltip, curve
    │                             #   selection+glow, single left axis, wheel zoom,
    │                             #   y-fit-on-settle, reset. Thin back-compat shim
    │                             #   for window.drawTimeSeries if still referenced.
    ├── mgmt/
    │   ├── mgmt.html             # period row: + Prev/Next, + right-aligned "Live",
    │   │                         #   + range label, + fullscreen toggle, + reset btn
    │   ├── mgmt.css              # period-row layout (right-aligned Live), fullscreen
    │   │                         #   overlay + sibling hiding, tooltip, glow, controls
    │   └── mgmt.js               # aligned-period from/to math (tz-aware), anchor +
    │                             #   Prev/Next state, Live buffer (bounded) fed from
    │                             #   the WS message, wire controller callbacks
    └── i18n/
        ├── en.json de.json fr.json uk.json   # + mgmt_plot_* keys (Live, Fullscreen,
        │                                     #   Exit, Prev, Next, Reset, no-data,
        │                                     #   range label formats)

tools/check_i18n.py                # run after i18n edits to verify pack parity
```

**Structure Decision**: Embedded SPA, front-end-only change. `chart.js` becomes a
stateful controller but remains framework-free and self-contained so the portal or
future pages can reuse it, consistent with the `www/common/` convention. Data
ownership (fetch, downsample, live buffer, timezone math) stays in `mgmt.js`; the
controller only renders and emits interaction events.

## Complexity Tracking

> No Constitution Check violations. Section intentionally empty.
