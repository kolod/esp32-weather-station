# Data Model: Local Hardware Emulator

**Feature**: 003-hardware-emulator | **Date**: 2026-07-12

All state is in-memory, owned by a single `EmulatorState` object guarded by one
`threading.Lock` (requests are short; one lock is sufficient and race-free for
multi-tab polling). Everything resets on process restart.

## EmulatorState (device snapshot)

Mirrors what `handlers_mgmt.c:api_status` reports.

| Field | Type | Initial | Notes / validation |
|-------|------|---------|--------------------|
| `boot_monotonic` | float | process start | uptime = now − boot; reset by simulated OTA reboot |
| `tz_name` | str | `"UTC"` | must exist in timezone catalog, else `unknown_timezone` |
| `time_mode` | str | `"local"` | `"local"` \| `"utc"`, else `invalid_time_mode` |
| `temp_unit` | str | `"C"` | `"C"` \| `"F"`, else `invalid_temp_unit` |
| `time_synced` | bool | scenario | false when `time-not-synced` scenario active |
| `time_source` | str | `"ntp"` | `"none"` \| `"rtc"` \| `"ntp"`; `"none"` when not synced |
| `time_last_sync` | int\|None | now − 3600 | epoch seconds or JSON `null` |
| `fw_version` | str | `"emu-0.1.0"` | bumped suffix after simulated OTA reboot |
| `wifi` | object | connected | `{state, ssid, rssi, ip}`; state ∈ `idle, provisioning_ap, connecting, connected, retrying, ap_fallback` |
| `history_records` | int | derived | count of history entries |
| `storage_free_kb` | int | `1024` | scenario-adjustable (storage-low edge state) |

## Scenario

Developer-selected behaviors (clarification: CLI initial + runtime switch).

| Field | Type | Default | Effect |
|-------|------|---------|--------|
| `join_outcome` | str | `"success"` | `success` \| `auth` \| `not_found` — resolution of the next join attempt |
| `ota_outcome` | str | `"success"` | `success` \| `invalid_image` \| `write_error` |
| `sensor_valid` | bool | `true` | false → `/api/status` has `temperature_valid:false` |
| `time_synced` | bool | `true` | false → `time_synced:false`, `time_source:"none"`, `time_last_sync:null` |
| `wifi_connected` | bool | `true` | false → mgmt status wifi block shows `retrying`, empty ssid/ip |

Transitions: any field may change at runtime via `PUT /emu/scenario`; changes
affect *subsequent* requests/attempts only (an in-flight join/OTA session keeps
the outcome it started with).

## SyntheticSensor & History

- `temperature_c(t) = 21.0 + 4.0·sin(2π·(t−t₀)/86400) + noise(t)`, noise deterministic (seeded hash of minute index), |noise| ≤ 0.3.
- **HistoryRecord**: `(timestamp: u32 epoch, temperature: float)` — generated at startup for the past `--history-hours` (default 24) at 60 s intervals; a background-free lazy append adds records as time passes (generated on read, no timer thread needed).
- Query: `from ≤ timestamp ≤ to` (defaults 0 / UINT32_MAX), ascending, same as `history_query()`.
- Uniqueness: timestamps strictly increasing; count reported as `history_records`.

## ScanNetwork (fixed catalog)

`{ssid: str ≤32, rssi: int (−30…−90), secure: bool}` — ~8 entries, e.g.:

| SSID | rssi | secure | role |
|------|------|--------|------|
| `HomeNet` | −45 | true | joins per `join_outcome` |
| `Emu-WrongPass` | −52 | true | **always** resolves `failed/auth` |
| `Emu-NotFound` | −80 | true | **always** resolves `failed/not_found` |
| `CoffeeShop Free` | −70 | false | open network (no password UI path) |
| …plus varied filler entries | | | realistic list rendering |

## JoinSession (state machine)

Mirrors `portal_server.c` join tracking.

```text
idle ──POST /api/wifi (valid)──▶ connecting ──after ~2 s──▶ connected(suffix="A1B2")
                                     │
                                     └──────────────────▶ failed(reason: "auth" | "not_found")
```

- Only one session; a new `POST /api/wifi` restarts it (device allows re-attempts).
- Validation before any transition: `ssid` present, 1–32 chars else `invalid_ssid`; `password` ≤ 63 chars else `password_too_long`; empty body → `empty_body`. Optional `tz_name` applied to EmulatorState if valid (invalid silently ignored — device behavior).
- Outcome: designated SSID override, else `scenario.join_outcome`.
- `suffix`: fixed 4-hex-char device suffix (e.g. `"A1B2"`), constant per run.

## OtaSession (state machine)

Mirrors `handlers_ota.c` (`ota_session_t`).

```text
idle ──POST /api/ota──▶ receiving(progress_pct 0→100 as body streams)
        │ (reject: 409 if receiving; 507 if len>3 MB or ≤0)
        ▼
    validating ──scenario success──▶ applied_pending_reboot ──3 s──▶ [simulated reboot] → idle
        │                                                            (uptime 0, fw_version bumped)
        └──scenario invalid_image──▶ failed(error="invalid_image")   [HTTP 400]
        └──scenario write_error───▶ failed(error="write_error")      [HTTP 500]
```

- `error` field: `null` unless `failed`.
- A `failed` or completed session returns to acceptable state for the next POST (device allows retry from `idle`/`failed`).

## TimezoneCatalog

Static list of ~25 IANA zone names (always includes `"UTC"`). Used by
`GET /api/timezones` (both ports) and by `PUT /api/config` / `POST /api/wifi`
timezone validation. Single source in the script so list and validation can't drift.

## Language

- Supported: `en`, `de`, `fr`, `uk`; default `en`.
- `accept_language_pick(header) -> lang`: tokenizes `Accept-Language`, primary-subtag match, highest q wins, ties → first occurrence; empty/None → `en`. Behavior must satisfy the eight cases in `components/web_server/test/test_i18n.c`.
