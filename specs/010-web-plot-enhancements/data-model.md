# Phase 1 Data Model: Web Interface Plot Enhancements

No persistent or server-side data. This describes the **client-side view-state**
that `mgmt.js` and the `chart.js` controller manage in memory for the session.
Ownership: `mgmt.js` holds durable choices and data; the controller holds transient
render/interaction state.

## Entity: PlotViewState  *(owned by `mgmt.js`)*

The single source of truth for what the history card is showing.

| Field | Type | Values / rules |
|---|---|---|
| `mode` | enum | `'history'` \| `'live'`. Default `'history'`. |
| `period` | enum | `'day'` \| `'week'` \| `'month'` \| `'all'`. Default `'day'`. Ignored when `mode === 'live'`. |
| `anchor` | epoch seconds | A time inside the period currently shown. Default `now`. Not used for `'all'` / `'live'`. |
| `selectedSeries` | int \| null | Index into `series[]`, or `null` for no selection. Default `null`. Reset to `null` if the index is absent after a data reload (FR-012). |
| `tzName` | string | From `status.tz_name`; `'UTC'` until first status arrives. Drives boundary math and tooltip time. |

**Derived (not stored):**

- `periodWindow = { from, to }` epoch seconds — computed from `period` + `anchor`
  + `tzName` (research R2). `to` clamped to `now`.
- `isCurrentPeriod` — true when `periodWindow` contains `now`; disables **Next**
  (FR-022).
- `rangeLabel` — localized string for the on-screen date / date-range (FR-024),
  format chosen by `period`.

**Transitions:**

| From | Event | To |
|---|---|---|
| any | click period button `P` | `mode='history'`, `period=P`, `anchor=now`, zoom reset |
| history | click **Prev** | `anchor -= onePeriod(period, anchor)` (calendar step) |
| history | click **Next** (and not `isCurrentPeriod`) | `anchor += onePeriod(...)`; clamp so window end ≤ next-period-end containing `now` |
| any | click **Live** | `mode='live'`, zoom reset, selection kept if series still present |
| live | click any period button | `mode='history'` (see row 1) |
| any | controller `onSelect(i)` | `selectedSeries = i` (or `null` to clear) |
| any | controller `onZoomChange` | store nothing durable; only `isZoomed` flag mirrored for the Reset button |

## Entity: SeriesInput  *(passed `mgmt.js` → controller via `setData`)*

One measurement channel. 1–3 present (temperature always; pressure if any non-null;
humidity if any non-null) — same rule as today.

| Field | Type | Notes |
|---|---|---|
| `label` | string | Localized (`mgmt_legend_*`). |
| `unit` | string | `'°C'` \| `'hPa'` \| `'%'`. Shown in axis caption + tooltip. |
| `color` | string | Existing palette: temp `#4fc3f7`, pressure `#ffb74d`, humidity `#81c784`. |
| `values` | (number\|null)[] | Parallel to `times[]`; `null` = gap. |

Plus the shared `times: number[]` (epoch seconds, ascending), parallel to every
series' `values`.

## Entity: ChartRenderState  *(owned by the `chart.js` controller)*

Transient; rebuilt on `setData`, mutated by pointer/wheel, never persisted.

| Field | Type | Meaning |
|---|---|---|
| `domain` | `{ t0, t1 }` epoch | X extent actually drawn. Starts = `periodWindow`; narrowed by wheel zoom, clamped to `periodWindow`. |
| `yFit` | `{ lo, hi }` | Current left-axis extent. Frozen during a wheel gesture; recomputed on settle from data in `domain` for `selectedSeries ?? 0`. |
| `hover` | `{ x, index } \| null` | Snapped crosshair position + nearest sample index; `null` when pointer is outside the plot rect. |
| `selected` | int \| null | Mirror of `PlotViewState.selectedSeries`, set via `setSelected`. |
| `isZoomed` | bool | `domain` narrower than `periodWindow`. Drives `onZoomChange` + Reset visibility. |
| `wheelSettleTimer` | timer id | Debounce handle for the post-gesture y-refit (~150 ms). |
| geometry | `{ PAD, plotW, plotH, xOf, yOf }` | Recomputed each repaint from canvas CSS size (DPR-aware, as today). `PAD.right` reduced — no right axis. |

**Interaction rules:**

- `hover` set on `pointermove`/`pointerdown` inside the plot rect; cleared on
  `pointerleave` / `pointercancel` / touch end (FR-004).
- Line hit-test on `pointerup` (that did not drag): nearest segment ≤ 6 px →
  `onSelect(seriesIndex)`; click on empty plot or on the already-selected series →
  `onSelect(null)` (FR-007, FR-010).
- Legend hit-rects: click → same `onSelect`.
- `wheel` (non-passive): update `domain` about pointer epoch, clamp, set
  `isZoomed`, `preventDefault()`, (re)arm `wheelSettleTimer`.
- On settle: recompute `yFit` from visible data, repaint, no-op if unchanged.

## Entity: LiveBuffer  *(owned by `mgmt.js`, module scope)*

Bounded in-RAM history of readings seen this session (research R7, R8).

| Field | Type | Rule |
|---|---|---|
| `entries` | `{ t, temperature, pressure, humidity }[]` | Appended on every WS message and poll-fallback response, in both modes. |
| `CAP` | const | `10000`. On overflow, drop from the front. |

- `t` = `msg.now` (epoch seconds). Entry skipped if `msg.now` missing or
  `temperature_valid` false.
- `pressure` / `humidity` = value if the corresponding `*_valid` flag is set and
  the sensor supplies it, else `null`.
- Independent of the `ws` object lifecycle — survives reconnect (FR-036).
- When `mode === 'live'`: `times` = `entries.map(e => e.t)`, series built from the
  same non-null rules as history; `periodWindow` = `{ from: entries[0].t, to: now }`.
  Empty buffer → empty-state message (FR-035).

## Entity: CrosshairReadout  *(derived, controller → tooltip DOM)*

Not stored; computed each `hover` change.

| Field | Type | Notes |
|---|---|---|
| `time` | string | `times[hover.index]` formatted in `tzName`; granularity (date vs. time) by `domain` span. |
| `rows` | `{ label, color, text }[]` | One per visible series. `text` = `value.toFixed(prec) + ' ' + unit`, or localized "no data" when `null`/`NaN` (FR-003). |
| `anchorX` | number | Screen x for tooltip placement; box repositioned to stay in-bounds (FR-005). |

## Relationships

```
PlotViewState 1──1 LiveBuffer        (consulted only when mode='live')
PlotViewState 1──* SeriesInput       (rebuilt on every data load)
PlotViewState.periodWindow ──drives──> ChartRenderState.domain (initial)
ChartRenderState.selected  ◄─sync──   PlotViewState.selectedSeries
ChartRenderState.hover ──derives──> CrosshairReadout
ChartRenderState.isZoomed ──toggles──> Reset control (DOM)
PlotViewState.isCurrentPeriod ──disables──> Next control (DOM)
```

## Validation rules (from FRs)

- At least one series is always drawn; if `selectedSeries` points past the array
  after a reload, set it to `null` (FR-012, FR-016).
- `domain` is always within `periodWindow` (wheel clamp, FR-039).
- `to` in any `/api/history` request ≤ `now` (research R2).
- `Next` is a no-op when `isCurrentPeriod` (FR-022).
- Tooltip never shows a number for a `null` sample (FR-003).
- LiveBuffer length ≤ `CAP` at all times (FR-037).
