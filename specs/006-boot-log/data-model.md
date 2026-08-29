# Data Model: Boot Log (006)

## 1. Boot Log Buffer (RAM)

Static state owned by the `boot_log` component:

| Field | Type | Description |
|-------|------|-------------|
| `buf` | `char[4096]` (static) | Line-oriented UTF-8 text, exactly as ESP-IDF log formatting produced it. |
| `len` | `size_t` | Bytes used, `0 ≤ len ≤ 4096`. |
| `state` | enum | Capture lifecycle, see §2. |
| `truncated` | `bool` | Set once when a line was discarded for lack of space. |
| `mutex` | FreeRTOS mutex | Serializes appends from concurrent tasks (sensor task, display task). |

**Validation rules**
- Append is all-or-nothing per line: a line that does not fit in the remaining space (minus the 48-byte reserved tail) is discarded whole — never split, never wraps (FR-004).
- On first discard, the marker line `[boot log full - further messages discarded]\n` (ASCII) is written into the reserved tail and `truncated` is set.
- Only lines whose tag prefix-matches the allowlist are appended (research D1): `sensor`, `bmp280`, `display`, `ds18b20`, `onewire`, `i2c.master`, `lcd_panel`, `spi`.
- ANSI color escape sequences are stripped before appending.

## 2. Capture Lifecycle (state transitions)

```text
INACTIVE ──boot_log_init()──▶ CAPTURING ──boot_log_close()──▶ CLOSED
```

| State | Entered by | Behavior |
|-------|-----------|----------|
| `INACTIVE` | power-on default | Hook not installed; appends impossible. |
| `CAPTURING` | `boot_log_init()` — called in `app_main` immediately after the LittleFS mount, before `sensor_start()`/`display_start()` | Stale `/storage/boot.log` deleted (FR-003). vprintf tee installed. Allowlisted lines appended to `buf`. |
| `CLOSED` | `boot_log_close()` — called at the end of `app_main`, after the 10 s OTA self-check | Appends stop (hook keeps forwarding to serial only). `buf[0..len)` written once to `/storage/boot.log`; write failure ignored (FR-008). Buffer retained in RAM for the web handler fallback. |

There are no other transitions; init and close are each called exactly once per boot.

## 3. Boot Log File (persistent)

| Property | Value |
|----------|-------|
| Path | `/storage/boot.log` (LittleFS `storage` partition) |
| Format | Plain text UTF-8, newline-delimited; each line `L (millis) tag: message` (research D7) |
| Max size | 4096 bytes (SC-003) — equals the RAM buffer cap, enforced at append time |
| Lifetime | Deleted at `boot_log_init()`, created at `boot_log_close()` → always reflects the most recent completed boot capture only |

## 4. Boot Log Section (management UI)

| Element | id / key | Behavior |
|---------|----------|----------|
| Card heading | `data-i18n="mgmt_heading_bootlog"` | "Boot Log" |
| Content | `<pre id="bootlog-content">` | Filled from `fetch('/api/boot.log')` on page load; scrollable, monospace. |
| Download link | `<a id="btn-bootlog" href="/api/boot.log" download="boot.log" data-i18n="mgmt_btn_bootlog">` | Saves the same bytes the view shows (US2). |
| Unavailable message | `data-i18n="mgmt_bootlog_unavailable"` | Shown instead of `<pre>` + link on non-200/fetch error (FR-007). |

## 5. i18n keys (FR-009, SC-005)

Added to `components/web_server/www/i18n/{en,de,fr,uk}.json`; parity enforced by `tools/check_i18n.py`:

| Key | en |
|-----|----|
| `mgmt_heading_bootlog` | Boot Log |
| `mgmt_btn_bootlog` | Download boot.log |
| `mgmt_bootlog_unavailable` | Boot log not available. |

## 6. Emulator scenario extension

`hw_emulator.py` scenario object gains one field:

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `bootlog` | `"present"` \| `"missing"` | `"present"` | `present`: `/api/boot.log` returns a synthetic log consistent with the active `sensor` kind; `missing`: returns the 404 error shape. |
