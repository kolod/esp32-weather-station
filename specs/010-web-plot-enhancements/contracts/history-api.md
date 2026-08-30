# Contract: `GET /api/history` — aligned & past-period queries (endpoint unchanged)

**No firmware change.** `api_history_get` already parses `from` / `to` as optional
epoch-second query params (`handlers_mgmt.c`). This feature only changes how the
**client** computes those values: from calendar-aligned, navigable windows instead
of rolling `now - N` windows.

## Request

```
GET /api/history?from=<epoch_seconds>&to=<epoch_seconds>
```

| Param | Required | Default | Meaning |
|-------|----------|---------|---------|
| `from` | no | `0` | lower bound, UTC epoch seconds (inclusive) |
| `to`   | no | `UINT32_MAX` | upper bound, UTC epoch seconds (inclusive) |

The server-side query buffer is 64 bytes; `from=<10>&to=<10>` (~28 chars) fits with
margin.

## Client window computation (new)

`anchor` = an epoch inside the period being viewed (starts at `now`, moved by
Prev/Next). `tz` = `status.tz_name` (default `UTC`). `localMidnight(epoch, tz)` and
month/week helpers per research.md R2.

| Period | `from` | `to` |
|--------|--------|------|
| **Day** | `localMidnight(anchor, tz)` | `min(from + 24h, now)` |
| **Week** | `localMidnight(mondayOnOrBefore(anchor, tz), tz)` | `min(from + 7d, now)` |
| **Month** | `localMidnight(firstOfMonth(anchor, tz), tz)` | `min(firstOfNextMonth(anchor, tz), now)` |
| **All** | `0` | `now` |
| **Live** | *(no request — client buffer)* | — |

- The **drawn** x-domain is always the full nominal window (`from` .. nominal end),
  even though `to` is clamped to `now` for the fetch — so "today" always renders
  00:00–24:00 with the afternoon empty. (FR-020)
- **Next** is disabled when the nominal window already contains `now`. (FR-022)
- DST: a Day window may be 23 h or 25 h wide; sample x-positions stay correct
  because both axis and points use the same epoch → x mapping. (spec Edge Cases)

## Response (unchanged)

`200 application/json`, streamed:

```json
{ "records": [
  { "timestamp": 1724930000, "temperature": 21.37, "pressure": 1008.4, "humidity": 47.0 },
  { "timestamp": 1724930300, "temperature": 21.40, "pressure": null,    "humidity": null }
] }
```

- Chronological. `pressure` / `humidity` `null` ⇒ per-series gap.
- Invalid-temperature records already omitted server-side.

## Client processing (unchanged except domain)

1. Filter to `typeof r.temperature === 'number'`.
2. Empty ⇒ localized empty-state (`mgmt_history_empty`), skip draw. Prev/Next stay
   usable. (FR-023)
3. `> ~400` records ⇒ bucket-average to ≈400 points (display only).
4. `hasPressure` / `hasHumidity` ⇒ which series to include.
5. `chart.setData({ times, series })` then `chart.setPeriodWindow({ from, to: nominalEnd })`.

## Race handling (new)

Tag each fetch with the `PlotViewState` it was issued for (mode + period + anchor).
On resolve, drop the response if that state no longer matches — a slow reply for a
superseded period must not overwrite the current plot. (spec Edge Cases)

## Unchanged sibling endpoints

- `GET /api/history.csv` — full-history CSV, untouched (still bottom of the card).
- `wss://<host>/api/ws` — feeds `LiveBuffer`; message shape per feature 009
  `contracts/ws-readings.md` (fields used here: `now`, `temperature_c`,
  `temperature_valid`, `pressure_hpa`, `pressure_valid`, `humidity_pct`,
  `humidity_valid`, `sensor`, `tz_name`).
