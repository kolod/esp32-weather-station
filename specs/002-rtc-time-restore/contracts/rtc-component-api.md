# Contract: `rtc_time` Component Public API

**Feature**: 002-rtc-time-restore | Header: `components/rtc_time/rtc_time.h`

Consumers: `main` (boot restore), `wifi_mgr` (sync notification), `web_server` (status). The component has no task of its own and performs no I/O — all operations are memory reads/writes and complete in microseconds.

## Functions

### `void rtc_time_restore(void)`

Boot-time restore check. Must be called exactly once, after NVS/event-loop init and **before** the display and web_server tasks start.

- Evaluates the restore decision (data-model.md §3) against the RTC-memory validity record and current system time.
- On success: sets `app_state.time_source = APP_TIME_SOURCE_RTC` and posts `APP_EVT_TIME_RESTORED`. Does **not** modify system time (ESP-IDF already carried it).
- On failure: leaves `time_source = APP_TIME_SOURCE_NONE`. Logs the reason (record corrupt / below build epoch / above upper bound) at INFO.
- Never blocks, never returns an error — a failed restore is a normal outcome (FR-006).

### `void rtc_time_mark_synced(void)`

Called by `wifi_mgr`'s SNTP sync callback after every successful synchronization, after the system clock has been updated by newlib.

- Rewrites the validity record (data-model.md §1) with `last_sync_utc = time(NULL)`, CRC computed last.
- Sets `app_state.time_source = APP_TIME_SOURCE_NTP`.
- Idempotent; called repeatedly (hourly SNTP polls).

### `app_time_source_t rtc_time_source(void)`

Returns the current time-source state (convenience accessor over `app_state.time_source`).

### `int64_t rtc_time_last_sync(void)`

Returns `last_sync_utc` from the validity record if it is intact, else `-1`. Used by the `/api/status` handler (`time_last_sync: null` when `-1`).

## Ordering & concurrency contract

- `rtc_time_restore()` runs single-threaded during boot, before consumers exist.
- `rtc_time_mark_synced()` is invoked from the SNTP/LwIP callback context; the record update is a small struct write finished with the CRC field, so readers (`rtc_time_last_sync`) at worst see a CRC-invalid record transiently — which reads as "absent", never as wrong data.
- State only increases in trust: `NONE → RTC → NTP` (data-model.md §4); the component never demotes the state.

## Compile-time contract

- `BUILD_EPOCH` (Unix seconds, UTC, of the build) is injected as a compile definition by the component's own CMakeLists; no other component may depend on it.
- `sdkconfig.defaults` pins the ESP-IDF system-time source to "RTC and high-resolution timer"; the component contains a `_Static_assert`/`#error` guard on the corresponding `CONFIG_` macro so a config drift fails the build, not the field behavior.

## Events (defined in `app_ctx`)

| Event | Posted by | When | Consumers |
|---|---|---|---|
| `APP_EVT_TIME_RESTORED` (new) | `rtc_time_restore()` | Valid time restored from RTC at boot | `display` (show time immediately) |
| `APP_EVT_TIME_SYNCED` (existing) | `wifi_mgr` | First SNTP sync of this boot | `display`, others (unchanged) |
