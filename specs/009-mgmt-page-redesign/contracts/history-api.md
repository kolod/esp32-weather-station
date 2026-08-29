# Contract: `GET /api/history` — period query (existing endpoint, no firmware change)

The history plot reuses the endpoint as-is. Documented here so the client contract is
explicit; **no server changes are in scope for this feature.**

## Request

```
GET /api/history?from=<epoch_seconds>&to=<epoch_seconds>
```

| Param | Required | Default | Meaning |
|-------|----------|---------|---------|
| `from` | no | `0` | lower bound, UTC epoch seconds (inclusive) |
| `to`   | no | `UINT32_MAX` | upper bound, UTC epoch seconds (inclusive) |

Client maps the period selector to `from`:

| Selector | `from` | `to` |
|----------|--------|------|
| Day (default) | `now - 86400` | `now` |
| Week | `now - 604800` | `now` |
| Month | `now - 2592000` | `now` |
| All | `0` | `now` |

## Response

`200 application/json`, streamed:

```json
{ "records": [
  { "timestamp": 1724930000, "temperature": 21.37, "pressure": 1008.4, "humidity": 47.0 },
  { "timestamp": 1724930300, "temperature": 21.40, "pressure": null,    "humidity": null }
] }
```

- `records` is chronological.
- `pressure` / `humidity` are `null` for samples without that channel (pre-005/007
  records, or non-BME280 operation) — client treats `null` as a gap per series.
- Records with an invalid temperature flag are already omitted server-side.

## Client processing

1. `hasPressure = records.some(r => r.pressure != null)`,
   `hasHumidity = records.some(r => r.humidity != null)` → decide which series to plot.
2. If `records.length === 0` → render the localized empty-state (`mgmt_history_empty`),
   do not draw the chart (FR-017).
3. If `records.length > ~400` → bucket-average to ≈ 400 points before drawing
   (display-only; FR-018).
4. Draw via `chart.js` `drawTimeSeries(canvas, series, opts)`.

## Unchanged sibling endpoints

- `GET /api/history.csv` — full-history CSV download; **moved in the DOM to the bottom
  of the history card**, behavior and byte output unchanged (FR-020, FR-021, SC-007).
- `GET /api/status` — remains the WebSocket fallback source (contract ws-readings.md).
- `GET /api/boot.log` — still the source for the inline boot-log view; only the
  download button is removed (FR-026/FR-027).
