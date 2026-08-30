# Contract: `chart.js` — `createTimeSeriesChart()` UI API

`www/common/chart.js` exposes one global factory. `mgmt.js` is the only consumer.
Framework-free, no external deps, CSP-safe (Principle IV).

## Construction

```js
const chart = window.createTimeSeriesChart(canvasEl, {
  tzName: 'UTC',              // IANA name; used for x-axis + tooltip time formatting
  noDataText: 'no data',      // localized, for tooltip gaps
  onSelect(index /* int|null */) {},   // user clicked a line or legend entry
  onZoomChange(isZoomed /* bool */) {}, // viewport narrowed / restored to full period
});
```

The controller binds `pointer*`, `wheel` (non-passive), and a `ResizeObserver` on
`canvasEl`. It does **not** bind `keydown` (Escape/fullscreen is `mgmt.js`).

## Methods

| Method | Purpose |
|---|---|
| `setData({ times, series })` | Replace the dataset. `times`: ascending epoch-seconds `number[]`. `series`: `[{ label, unit, color, values }]`, `values` parallel to `times`, `null` = gap. Rebuilds render state; preserves `selected` if the index still exists, else clears it and fires `onSelect(null)`. |
| `setPeriodWindow({ from, to })` | Set the full drawn time domain (epoch seconds). Resets any zoom, refits y, repaints. Call on every period / Prev / Next / Live change. |
| `setSelected(index /* int|null */)` | Programmatic selection (e.g. restoring state). Does **not** re-fire `onSelect`. Repaints: glow + left-axis switch. |
| `resetZoom()` | Restore domain to the last `setPeriodWindow`, refit y, repaint. Fires `onZoomChange(false)`. |
| `setTz(tzName)` | Update timezone for labels/tooltip (when status arrives after first paint). |
| `redraw()` | Force a repaint (e.g. after entering fullscreen, though the ResizeObserver usually covers it). |
| `destroy()` | Remove listeners and the observer. |

## Behavioural guarantees

1. **Fixed domain** — the x-axis always spans the `setPeriodWindow` range (or the
   current zoom sub-range), never the data's own extent. Partial data fills part of
   the width. (FR-017..FR-020)
2. **Single left axis** — no right-side axis or labels are ever drawn. (FR-013)
   Axis ticks + caption come from `series[selected]` when a selection exists, else
   `series[0]`. (FR-014, FR-015)
3. **All series drawn** — non-axis series keep independent vertical scaling so
   their shape is visible; selection changes only the annotation, not which
   curves render. (FR-016)
4. **Crosshair + tooltip** — while the pointer is inside the plot rect, a vertical
   line is drawn at the nearest sample and the tooltip shows that sample's time +
   each series' value (or `noDataText`). Both clear on `pointerleave` /
   `pointercancel` / touch end. Tooltip box stays within the canvas bounds.
   (FR-001..FR-006)
5. **Selection glow** — selected series: halo + bright stroke; others dimmed to
   ~35% alpha. Legend marks the selection. (FR-009, FR-011)
6. **Wheel zoom** — `wheel` zooms the domain about the epoch under the pointer,
   clamped to the `setPeriodWindow` range, and calls `preventDefault()` (no page
   scroll). Y-axis is frozen during the gesture; ~150 ms after the last wheel
   event the y-axis refits to data visible in the domain (selected series, else
   series 0). `onZoomChange(true)` fires when the domain first narrows.
   (FR-038..FR-043)
7. **DPR-aware** — canvas backing store scaled by `devicePixelRatio`; repaint on
   observed resize. (FR-029, FR-046)
8. **Degrades** — 0 or 1 points, or all-`null` series: no crash; caller shows the
   empty state when `times.length < 2`.

## Back-compat shim

`window.drawTimeSeries(canvas, spec)` is retained as a one-shot wrapper
(`createTimeSeriesChart(canvas, {}).setData(spec)` with a rolling domain) for any
external caller. The portal does not use it; can be dropped if grep confirms no
references.

## Caller responsibilities (`mgmt.js`)

- Fetch history for `periodWindow`, filter invalid temps, downsample to ≈400
  points (unchanged logic), call `setData` + `setPeriodWindow`.
- Maintain the Live buffer and, in live mode, feed it through the same pipeline.
- Own the Prev/Next/Live/Fullscreen/range-label DOM and the Reset button
  visibility (driven by `onZoomChange`).
- Persist `selectedSeries` across reloads and call `setSelected` after `setData`.
