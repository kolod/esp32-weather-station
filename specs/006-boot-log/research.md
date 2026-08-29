# Research: Boot Log (006)

All Technical Context unknowns resolved. Decisions numbered D1–D7.

## D1 — Capture mechanism: `esp_log_set_vprintf()` tee with a tag allowlist

**Decision**: Install a custom vprintf via `esp_log_set_vprintf()` that (a) always forwards to the previously installed vprintf (serial output unchanged), and (b) additionally appends the formatted line to the boot-log buffer **iff** its tag matches an allowlist covering the sensor and display subsystems and the drivers they sit on: `sensor`, `bmp280`, `display`, `ds18b20`, `onewire`, `i2c.master`, `lcd_panel*`, `spi` (prefix match against a small static table). The tag is extracted from the formatted line (ESP-IDF format: `L (millis) tag: message`).

**Rationale**:
- Captures the diagnostic lines that already exist in `sensor.c`/`bmp280.c` with zero call-site changes, **and** errors emitted inside IDF drivers (I2C timeouts, panel init failures) that an explicit `BOOT_LOG()` API would miss.
- The allowlist honors the spec assumption that only sensor/display messages are captured — notably it keeps WiFi logs (which can contain SSIDs) out of a file that is served over HTTP.
- Sensor and display init run in their own tasks *after* `boot_log_init()` is called (LittleFS mounts before `sensor_start()`/`display_start()` in `main.c`), so no allowlisted message can precede hook installation.

**Alternatives considered**:
- *Capture everything during the boot window* — simpler, but violates the spec's stated scope, fills 4 KB with WiFi/httpd noise, and leaks network details into a web-served file. Rejected.
- *Explicit `boot_log_printf()` API called from sensor/display code* — deterministic but misses driver-emitted errors (the most valuable lines when init fails) and duplicates every existing `ESP_LOG` call site. Rejected.
- *`esp_log` v2 handlers / `CONFIG_LOG_CUSTOM`* — heavier configuration surface for no gain at this scale. Rejected.

**Caveat (documented, accepted)**: `ESP_ERROR_CHECK` failures abort via `esp_system`'s panic path, which bypasses the vprintf hook — a hard display-init abort is not captured. Acceptable: after an abort the device reboots and never serves the web page anyway; the serial cable remains the tool for panic-level failures.

## D2 — Buffering & persistence: static 4 KB RAM buffer, single flush at a fixed close point

**Decision**: The hook appends into a static 4 KB buffer under a dedicated mutex (never calls `ESP_LOG` itself — no recursion). `boot_log_init()` (called in `app_main` immediately after the LittleFS mount) deletes any stale `/storage/boot.log` (FR-003) and installs the hook. `boot_log_close()` (called at the end of `app_main`, after the existing 10 s OTA self-check delay) stops capture and writes the buffer to `/storage/boot.log` in one operation. The buffer is retained in RAM after close so the web handler can fall back to it.

**Rationale**:
- **FR-008 by construction**: the log path (sensor/display tasks) performs only a bounded `memcpy` — no flash I/O can ever stall or fail a booting subsystem. The only file write happens at one well-defined point, and its failure is silently ignored (logged to serial only).
- Sensor detection and display init complete within ~2 s; the close point at ~10 s comfortably brackets them and gives the file a crisp "startup only" meaning (FR-003's "most recent boot only").
- One ≤ 4 KB write per boot is negligible LittleFS wear; deleting the stale file at init means a crash mid-boot leaves no misleading old log (spec edge case).

**Overflow behavior (FR-004)**: when a line does not fit, it is discarded and a one-time `[boot log full — further messages discarded]` marker is written into 32 reserved tail bytes. No wrap-around.

**Alternatives considered**:
- *Write-through `FILE*` with `fflush` per line* — flash latency inside the log hook blocks the calling task (sensor task logs on its 5 s cadence path); partial files on crash; more wear. Rejected.
- *Serve from RAM only, no file* — FR-001 mandates a persistent `boot.log` file; also loses post-close persistence if RAM were later reclaimed. Rejected.
- *Free the buffer after flush* — saves 4 KB but forces the web handler to depend solely on the file and breaks the pre-flush fallback (D4); heap at idle is ≥ 80 KB, so 4 KB static is cheap. Rejected.

## D3 — Message coverage: two small call-site additions

**Decision**:
1. `bmp280.c`: on detect failure emit `ESP_LOGW(TAG, "BMP280 not found at 0x76 or 0x77")` — the spec's acceptance scenario names this line verbatim; today only per-address `ESP_LOGD` (compiled out at default level) exists.
2. `display.c`: `panel_init()`/`display_task()` currently log **nothing**. Add `ESP_LOGI` progress lines: SPI bus + panel IO created, ST7789 panel initialized, LVGL port ready, UI initialized. Failure cases stay as `ESP_ERROR_CHECK` (see D1 caveat) — converting them to soft errors would change boot semantics and is out of scope.

**Rationale**: FR-002 requires display-init messages to exist before they can be captured; the additions are info-level and cost nothing at runtime.

## D4 — HTTP endpoint: `GET /api/boot.log`, plain text, RAM fallback

**Decision**: One endpoint on the management server (not the portal): `GET /api/boot.log` → `200 text/plain; charset=utf-8` with the log content. The handler reads `/storage/boot.log`; if the file is absent (first ~10 s of uptime, or storage write failed) it serves the live RAM buffer via `boot_log_get()`; if both are empty/unavailable → `404 {"error":"not_found"}` (matches the emulator's existing 404 shape). No `Content-Disposition` header — the same endpoint feeds both the in-page `fetch()` view and the download link.

**Rationale**: The mgmt server is HTTPS and already the diagnostic surface (`/api/status`, `/api/history.csv`); the portal is scoped out by the spec. The RAM fallback closes the gap where a fast STA connect makes the page reachable before the 10 s flush. Reusing one endpoint keeps view and download provably identical (US2 scenario 2).

**Alternatives considered**: `/boot.log` at root (breaks the `/api/*` convention); `Content-Disposition: attachment` (would force download on direct navigation and adds nothing — the anchor's `download` attribute already names the file, same as `history.csv` uses on its link... note `history.csv` sets the header because CSV is its only consumer; here the primary consumer is `fetch()`). Rejected.

## D5 — UI: Boot Log card on the management page

**Decision**: New `<section class="card bootlog">` between History and Help: localized `<h2>`, a `<a href="/api/boot.log" download="boot.log">` link styled like the CSV link, and a `<pre id="bootlog-content">` (CSS: `overflow-x auto`, capped height, monospace). `mgmt.js` fetches `/api/boot.log` once on load; non-200 or fetch error → localized "not available" text replaces the `<pre>` (FR-007). Download link is hidden in the unavailable state.

**i18n keys** (× en/de/fr/uk, enforced by `tools/check_i18n.py`):
- `mgmt_heading_bootlog` — "Boot Log"
- `mgmt_btn_bootlog` — "Download boot.log"
- `mgmt_bootlog_unavailable` — "Boot log not available."

## D6 — Emulator & tests

**Decision**: `MgmtHandler` gains route `("GET", "/api/boot.log") → h_bootlog`, serving a synthetic multi-line log whose content matches the active `--sensor` kind (e.g., BMP280-found vs. not-found lines) so it exercises realistic content. New scenario field `bootlog: "present" | "missing"` (default `present`, settable via `/emu/scenario` and a `--bootlog` CLI flag) — `missing` returns the 404 shape, driving the FR-007 UI path in tests. `test_hw_emulator.py` adds: 200 + `text/plain` + body ≤ 4096 bytes + sensor-consistent content; 404 shape on `missing`; mgmt.html contains the bootlog section ids; i18n keys present in all four packs.

**Rationale**: Mirrors the established emulator pattern (routes mirror `handlers_mgmt.c`; scenario switches mirror `--join-outcome`/`--sensor`); the spec explicitly assumes the emulator serves the same endpoint.

## D7 — Line format & timestamps

**Decision**: Lines are stored exactly as ESP-IDF formats them: `L (millis) tag: message\n` (e.g., `I (1834) sensor: Sensor mode: BMP280 (temperature + pressure)`). No re-formatting, no color codes (the tee strips ANSI escapes if color logging is enabled).

**Rationale**: Millis-since-boot satisfies the spec's "timestamped-or-sequenced" requirement, is what developers already read on serial (zero cognitive translation), and needs no wall-clock time — which is typically not yet set during early boot.
