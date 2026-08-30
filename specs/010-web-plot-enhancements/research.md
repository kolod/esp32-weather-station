# Phase 0 Research: Web Interface Plot Enhancements

All work is client-side JavaScript/CSS in the embedded management page. No
firmware, protocol, or endpoint questions remain open. The decisions below resolve
the spec's assumptions and the implementation choices that shape `chart.js` /
`mgmt.js`.

## R1 — Chart architecture: stateful controller vs. keep the stateless draw

**Decision**: Replace `window.drawTimeSeries(canvas, spec)` with a stateful factory
`createTimeSeriesChart(canvasEl, options)` returning a handle with `setData()`,
`setPeriodWindow()`, `setSelected()`, `resetZoom()`, `destroy()` and event
callbacks (`onSelect`, `onZoomChange`). The controller owns the canvas, the
pointer/wheel/resize listeners, and transient view state (hover position, zoom
viewport, selected series index). `mgmt.js` owns durable state (which period,
which anchor, live vs. history, the data arrays).

**Rationale**: Crosshair, selection, and wheel zoom are all stateful, event-driven
behaviours that must survive redraws. A pure `draw(spec)` function would force
`mgmt.js` to re-implement hit-testing and listener bookkeeping. One controller
keeps that logic in the reusable asset.

**Alternatives considered**: (a) Keep `drawTimeSeries` and layer listeners in
`mgmt.js` — rejected, spreads canvas geometry math across two files. (b) Adopt a
charting library (uPlot, Chart.js) — rejected, CSP `default-src 'self'` + Principle
IV forbid third-party scripts; even vendored, the smallest capable libs dwarf the
hand-rolled renderer. A thin `window.drawTimeSeries` shim can be kept if any other
page still calls it (portal does not).

## R2 — Calendar-aligned period boundaries in the device timezone

**Decision**: Compute period bounds from an *anchor* `Date` and the device
timezone string (`status.tz_name`, default `'UTC'`):

- **Day**: local midnight of the anchor day → +24 h (next local midnight).
- **Week**: local midnight of the Monday on/before the anchor → +7 days.
- **Month**: local midnight of day 1 of the anchor's month → local midnight of day
  1 of the next month.

Convert a local wall-clock time in a named timezone to a UTC epoch with a helper:
format a guess `Date` with `Intl.DateTimeFormat(..., { timeZone, hour12:false,
year/month/day/hour/minute/second })`, diff the formatted parts against the target
wall-clock to get the zone offset, correct once (a second correction handles the
DST-changeover hour). This is the standard offset-inversion trick and needs no
tz database.

`from` = epoch of period start, `to` = `min(epoch of period end, now)`.

**Rationale**: `Intl.DateTimeFormat` with `timeZone` is available in every target
browser and already used in `mgmt.js` for the clock. Anchor + width fully
describes Prev/Next. Clamping `to` at `now` avoids requesting the future.

**Alternatives considered**: Rolling windows (`now - 86400` etc., current
behaviour) — rejected by FR-017..020, they make positions drift and periods
incomparable. Shipping a tz library (Luxon/date-fns-tz) — rejected (CSP / size).
Doing boundary math in UTC only — rejected, "a day" must mean the user's local day.

## R3 — Week start day

**Decision**: Monday (ISO-8601), fixed. Documented as an assumption in the spec.

**Rationale**: The project's languages (en, de, fr, uk) are all Monday-first
locales; a fixed rule keeps the boundary math trivial and predictable. Revisit
only if a Sunday-first locale is added.

**Alternatives considered**: Derive from `navigator.language` /
`Intl.Locale.prototype.weekInfo` — `weekInfo` support is uneven and the payoff is
nil for the current locale set.

## R4 — Tooltip value lookup

**Decision**: On pointer move, map pointer x → epoch `t` via the current viewport,
binary-search the (ascending) `times` array for the nearest index, and report each
series' value at that index. Render "no data" (localized) when the value is
`null`/`NaN`. The crosshair is drawn at the *snapped* sample x (not the raw pointer
x) so the line, the tooltip time, and the reported values are mutually consistent.

**Rationale**: Data is already downsampled to ≈ 400 points; nearest-sample is
unambiguous, needs no interpolation, and snapping removes the "line says one thing,
numbers say another" problem. Spec allows interpolation but does not require it.

**Alternatives considered**: Linear interpolation between neighbours — more code,
and misleading across gaps. Per-series independent nearest (different x per series)
— confusing with one shared crosshair.

## R5 — Selection, glow, and the single left axis

**Decision**:
- **Hit-testing a line**: nearest-segment distance in screen space; select if
  within ~6 px. Legend entries are plain clickable hit-rects.
- **Glow**: draw the selected series twice — a wide, low-alpha stroke (halo) then
  the normal stroke at full width + brightness; dim unselected series to ~35%
  alpha.
- **Left axis**: when a series is selected, the left axis ticks/label come from
  that series' nice-extent + unit. When nothing is selected, they come from series
  0 (temperature), matching today. Unselected series keep their own independent
  vertical scaling so their shape stays visible (as the current chart does for
  series ≥ 1); only the axis annotation follows the selection.
- **Right axis**: removed entirely — drop the right-side tick loop and shrink
  `PAD.right` to a small margin for the last x-tick label.

**Rationale**: Satisfies FR-009/FR-013/FR-014/FR-016 with minimal geometry change.
Canvas `shadowBlur` gives a cheap glow but is slow on some mobile GPUs when
animated; the double-stroke halo is predictable and fast for a static repaint.

**Alternatives considered**: Re-scaling every series to the selected axis (so
values are directly comparable) — rejected, makes unselected curves collapse to
flat lines. `shadowBlur` glow — kept as a fallback if the double-stroke looks weak,
but not the primary approach.

## R6 — Wheel zoom on the time axis

**Decision**: Attach a non-passive `wheel` listener (`{ passive: false }`) on the
canvas; `preventDefault()` to stop page scroll (FR-043). Each event scales the
viewport `[vFrom, vTo]` about the epoch under the pointer by a factor
(`deltaY < 0` → ×0.85 zoom-in, else ×1/0.85), then clamps to the period bounds
(FR-039). During the gesture the y-scale is frozen. A trailing debounce (~150 ms
after the last wheel event) triggers a y-refit to the min/max of the data visible
in the viewport — of the selected series if any, else series 0 (FR-040/FR-041).
The chart is "zoomed" whenever the viewport is narrower than the full period; in
that state the Reset control is shown (FR-042) and calls `resetZoom()` →
viewport = full period + y-refit.

**Rationale**: Anchor-preserving zoom about the cursor is the expected UX
(maps/devtools). Freezing y during the gesture prevents nauseating vertical jitter;
refitting on settle gives the "curves auto-adjust" behaviour the spec asks for.
150 ms is below the perceptible-lag threshold yet coalesces a fast scroll.

**Alternatives considered**: Continuous y-refit every event — rejected (jitter,
spec explicitly says refit *after* scrolling). CSS `touch-action` pinch-zoom —
out of scope; spec covers wheel only, and pinch would fight page zoom.

## R7 — Live session buffer bound

**Decision**: A plain array in `mgmt.js`, one entry per WebSocket status message
(`{ t: msg.now, temperature, pressure, humidity }`), capped at **10 000** entries;
on overflow drop from the front (`shift`, or a head index to avoid O(n)). At the
station's ≥ 5 s sample cadence 10 000 entries ≈ 14 h; the cap is a safety valve,
not the normal working set. Memory ≈ 10 000 × ~40 B ≈ 0.4 MB — negligible.

**Rationale**: Bounds memory for an indefinitely open page (FR-037, SC-009) while
comfortably covering a long viewing session. The buffer is display-only and never
persisted, so losing the oldest points on overflow is acceptable.

**Alternatives considered**: Time-based eviction (keep last N hours) — equivalent,
but a count cap is simpler and directly bounds memory. `localStorage` persistence
— rejected, not required and adds quota-handling complexity.

## R8 — Live buffer across WS reconnect

**Decision**: The buffer lives in module scope in `mgmt.js`, independent of the
`ws` object's lifecycle. `ws.onmessage` appends to it unconditionally (in Live
mode and out). Reconnect (`connectWs` after `scheduleReconnect`) does not clear it;
the poll fallback (`refreshStatus`) also appends when it runs. Result: a gap in
the trace during the outage, then it resumes — exactly FR-036.

**Rationale**: Decoupling storage from transport is the minimal way to survive
reconnects. Appending from the poll fallback too keeps the Live view alive even if
the socket never comes back.

## R9 — Fullscreen implementation

**Decision**: CSS-class driven, not the Fullscreen API. Toggling
`body.plot-fullscreen` makes the history `<section>` `position: fixed; inset: 0;
z-index: 50` and hides every other top-level `<main>` child and the `<header>` via
`body.plot-fullscreen header, body.plot-fullscreen main > :not(.history){display:none}`.
A `keydown` handler exits on `Escape`. A `ResizeObserver` on the chart wrapper (used
in all modes) re-fits the canvas, so entering/leaving fullscreen and device
rotation are handled by the same path. The native Fullscreen API is *not* used
(its permission prompts and cross-browser quirks add risk for no benefit inside a
same-origin LAN page).

**Rationale**: Pure CSS is the most robust way to guarantee "all other interface
elements not shown" (FR-026) and it trivially preserves period/selection/zoom
state (FR-030) because the same DOM and controller instance stay mounted.

**Alternatives considered**: `element.requestFullscreen()` — heavier, and the
`fullscreenchange` event + Escape handling still needed anyway. Moving the canvas
into a separate overlay node — would reset the controller / canvas context.

## R10 — Rendering the fixed domain with sparse data

**Decision**: The controller always draws the x-axis across the full viewport
domain (period bounds or zoom range), independent of the data's own min/max time.
Points plot at `x = xOf(sample.t)`; if the data covers only part of the domain the
line simply occupies part of the width (FR-020). Gaps (`null`) break the polyline
as today.

**Rationale**: Decoupling the drawn domain from the data extent is the core change
that makes periods comparable; it is a one-line change to how `t0/t1/span` are
chosen (from the viewport, not `times[0]/times[last]`).

## Resolved spec assumptions

| Spec assumption | Resolution |
|---|---|
| Primary curve = temperature | Confirmed — series index 0, drives the axis when nothing selected (R5). |
| Device timezone for boundaries & tooltip | Confirmed — `status.tz_name`, default UTC (R2). |
| Week starts Monday | Confirmed — fixed (R3). |
| Live buffer "a few thousand" readings | Set to 10 000 with front-drop (R7). |
| Tooltip uses nearest sample | Confirmed — nearest, with crosshair snapped to it (R4). |
| Time-axis zoom only, no pan/drag | Confirmed — Prev/Next covers navigation (R6). |
| No external charting library | Confirmed — hand-rolled canvas (R1). |
| History retrieval/downsampling unchanged except range | Confirmed — only `from`/`to` change (R2, contracts/history-api.md). |
