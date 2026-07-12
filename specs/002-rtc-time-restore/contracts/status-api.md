# Contract: `/api/status` Additions

**Feature**: 002-rtc-time-restore | Extends feature 001's management HTTP API (`GET /api/status`, HTTPS, cert-authenticated)

## Changed response fields

The existing status JSON object gains two fields; all existing fields (`time_synced`, etc.) are unchanged for backward compatibility.

| Field | Type | Values / semantics |
|---|---|---|
| `time_source` | string | `"none"` — no trustworthy time this boot; `"rtc"` — restored from the battery-backed clock, not yet network-verified this boot; `"ntp"` — network-synchronized this boot (FR-007) |
| `time_last_sync` | number \| null | Unix seconds (UTC) of the most recent successful network synchronization ever recorded in the battery-backed record; `null` if no intact record exists |

## Invariants

- `time_source == "ntp"` ⟺ existing `time_synced == true`.
- `time_source != "none"` ⟺ the display is showing a time (FR-005 — same predicate everywhere).
- `time_last_sync` is non-null whenever `time_source == "rtc"` (a restore is only possible from a recorded sync) and whenever `time_source == "ntp"`.

## Example responses

Restored from RTC, network still down:

```json
{
  "time_synced": false,
  "time_source": "rtc",
  "time_last_sync": 1783947600,
  "...": "existing fields unchanged"
}
```

Cold boot, battery was absent, no network yet:

```json
{
  "time_synced": false,
  "time_source": "none",
  "time_last_sync": null,
  "...": "existing fields unchanged"
}
```

## UI note

The management page's device-status section renders the three states as: "Time: not available", "Time: from backup clock (last synced <datetime>)", "Time: synchronized (last synced <datetime>)". Localized like the rest of the page (en/de/fr/uk).
