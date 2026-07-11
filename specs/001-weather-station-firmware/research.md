# Phase 0 Research: ESP32 Weather Station Firmware

**Date**: 2026-07-11 | **Plan**: [plan.md](./plan.md)

All Technical Context unknowns are resolved below. Component versions verified against the ESP Component Registry on 2026-07-11.

## R1. ESP-IDF version and project scaffolding

- **Decision**: ESP-IDF v5.4.x (latest stable 5.x line), project created with `idf.py create-project esp32-weather-station`, target `esp32`, C17.
- **Rationale**: User mandates ESP-IDF and `idf.py create-project`. IDF 5.x is required by current versions of `esp_lvgl_port` and `ds18b20` components and ships `esp_lcd` ST7789 support, `esp_netif_sntp`, and mature OTA rollback.
- **Alternatives considered**: IDF 4.4 (EOL, older component APIs — rejected); PlatformIO/Arduino (contradicts explicit ESP-IDF requirement).

## R2. Display stack (ST7789V 250×135, landscape, no PSRAM)

- **Decision**: Built-in `esp_lcd` driver (`esp_lcd_new_panel_io_spi` + `esp_lcd_new_panel_st7789`) on SPI2 host, registered with `espressif/esp_lvgl_port` ^2.8.0 running LVGL 9. Landscape via LVGL rotation/swap-xy. Two DMA-capable partial buffers of ~1/8 screen (250×17×2 B ≈ 8.5 KB each, tune upward if heap allows) in internal RAM. Backlight on GPIO4 via LEDC PWM. All LVGL calls guarded by `lvgl_port_lock()/unlock()`.
- **Rationale**: Both components are Espressif-maintained (user named `esp_lvgl_port`); registry check confirms 2.8.0 supports LVGL 9 and documents exactly this three-step ST7789 flow. Partial buffers keep RAM use bounded without PSRAM.
- **Risk / verify on hardware**: user states 250×135 px; the common T-Display ST7789V panel is 240×135 with gap offsets (x≈40, y≈52/53 in landscape). Horizontal resolution and `esp_lcd_panel_set_gap()` values are `Kconfig` options with the user's 250×135 as default, verified against real glass in the first display task (visual test pattern in quickstart).
- **Alternatives considered**: raw `esp_lcd` + custom drawing (more code, no widget/i18n support); LVGL 8 (older, no reason).

## R3. Temperature sensing

- **Decision**: `espressif/ds18b20` ^0.4.0 over `onewire_bus` (RMT) on GPIO27. `sensor` task: trigger conversion → wait (750 ms max, 12-bit) → read, on a 5 s cadence; publish `{value_c, valid, timestamp}` to shared app state with an event on change/validity flip. CRC failures or missing device ⇒ `valid=false` (display/web show "unavailable" per FR-003).
- **Rationale**: User-named library; 0.4.0 current; enumeration API handles the single probe; RMT frees CPU vs bit-banging.
- **Alternatives considered**: custom GPIO 1-Wire (timing-fragile); sensor-hub mode of the component (unneeded indirection for one probe).

## R4. WiFi lifecycle: STA with NVS credentials, AP fallback, captive portal

- **Decision**:
  - Credentials stored via native ESP-IDF mechanism: `esp_wifi_set_storage(WIFI_STORAGE_FLASH)` so `esp_wifi_set_config()` persists SSID/password in the wifi NVS namespace ("prefer esp-idf methods" per user).
  - Boot: if stored SSID non-empty → STA connect with exponential-ish retry (e.g., 6 attempts / ~60 s window). On exhaustion or empty config → switch to `WIFI_MODE_APSTA`, SSID `weather-XXXX` (open), where `XXXX` = last 2 bytes of `esp_efuse_mac_get_default()` as 4 lowercase hex chars.
  - APSTA (not pure AP) so the portal can run a scan and attempt the user-submitted join without tearing down the hotspot; on confirmed `IP_EVENT_STA_GOT_IP` the AP is stopped (FR-009).
  - Captive portal detection: `captive_dns` component answers **all** DNS A-queries with the AP IP (192.168.4.1); HTTP server answers OS probe URLs (`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt`) with 302 → portal page.
  - Runtime reconnect: `WIFI_EVENT_STA_DISCONNECTED` → retry loop; prolonged failure re-enters AP fallback while display keeps running (FR-005/FR-006, US3).
- **Rationale**: Mirrors the proven IDF captive-portal example pattern; wifi-NVS storage is the most "esp-idf native" credential store and is power-loss safe (FR-019).
- **Alternatives considered**: custom NVS namespace for credentials (duplicates what esp_wifi already does); esp-idf provisioning manager / SoftAP-prov + app (user wants a browser captive portal, not a phone app); password-protected AP (breaks frictionless portal redirect, spec assumes open).

## R5. Web serving: HTTP portal (AP) + HTTPS management (STA)

- **Decision**: One `web_server` component/task managing two server instances:
  - **AP mode**: plain `esp_http_server` on :80 — captive portal (OS probes are plain HTTP; TLS would break redirection and the device cert isn't trusted yet).
  - **STA mode**: `esp_https_server` on :443 with the per-device certificate + key loaded from `/storage/certs/` (littlefs). Management page + JSON API + OTA upload. Max ~2 TLS sessions, connection keep-alive tuned down to bound heap (no PSRAM). Optional :80 listener that 301-redirects to https.
  - Static assets (HTML/CSS/JS, i18n string tables) gzipped at build time and embedded via `EMBED_FILES`; served with `Content-Encoding: gzip`.
- **Rationale**: `esp_https_server` is the IDF-native TLS front end (FR-011/FR-014); embedding assets in the app image means OTA updates UI and firmware atomically; gzip keeps the pages small for the portal.
- **Alternatives considered**: serving assets from littlefs (risks version skew with firmware after OTA); third-party web frameworks (unnecessary).

## R6. Portal localization (en/de/fr/uk) via Accept-Language

- **Decision**: Single portal HTML template; four compiled-in string tables (JSON). Server parses `Accept-Language` (q-values honored, primary-subtag match, fallback `en` per FR-022) and injects the chosen table (or sets a `lang` attribute the page's JS uses). A small pure-C `accept_language_pick()` in `web_server` with Unity tests.
- **Rationale**: Server-side selection is what FR-022 mandates; string tables keep translations reviewable and the binary small; pure-C parser is trivially unit-testable.
- **Alternatives considered**: client-side `navigator.language` (contradicts FR-022's header requirement); four separate pre-rendered pages (4× asset size, drift risk).

## R7. Time, timezone, DST

- **Decision**: `esp_netif_sntp` started on first `IP_EVENT_STA_GOT_IP` (pool: `pool.ntp.org`, smooth sync). Timezone stored in NVS as a pair {IANA name for the UI, POSIX TZ string for the C library}; a curated compiled-in table (~40 common zones incl. Europe/Kyiv, Europe/Berlin, Europe/Paris, UTC…) maps one to the other. `setenv("TZ", posix, 1); tzset();` applies DST rules automatically (FR-021). Between syncs the ESP32 RTC keeps counting (edge case: extended offline drift accepted). Display shows "--:--" until first sync (FR-003); left button toggles local/UTC rendering of the same epoch (FR-025).
- **Rationale**: POSIX TZ strings with embedded DST rules are the standard newlib mechanism on ESP-IDF — no timezone database needed on device.
- **Alternatives considered**: raw UTC-offset setting (no DST, violates FR-021); full IANA tzdb on device (hundreds of KB, unnecessary).

## R8. Buttons

- **Decision**: `espressif/button` (iot_button) component. Left = GPIO0, active-low, internal pull-up (strap pin — safe as input after boot). Right = GPIO35, active-low, **input-only pad, no internal pull-up** — relies on the board's external pull-up. Single-click events: left toggles time mode local↔UTC, right toggles °C↔°F; both write-through to NVS (FR-025/FR-026). Long-press (5 s) on left = factory reset (erase WiFi config + settings) — the optional extra from spec Assumptions.
- **Rationale**: iot_button provides debounce + click/long-press classification; GPIO35 hardware constraints (input-only, no pulls) dictate reliance on board pull-up.
- **Alternatives considered**: raw ISR + timer debounce (reinvents iot_button).

## R9. OTA update with rollback

- **Decision**: Two 3 MB OTA app partitions + `otadata`; `sdkconfig.defaults` sets `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. Upload path: HTTPS `POST /api/ota` (raw `application/octet-stream` streamed body) → `esp_ota_begin/write/end` → set boot partition → reboot. `esp_ota_end` validates image integrity; additionally the first received bytes are checked against `esp_app_desc_t` (magic + project name) to reject foreign images early (FR-015). After reboot the app runs a self-check (NVS mounts, display up, sensor task started) then `esp_ota_mark_app_valid_cancel_rollback()`; otherwise the bootloader rolls back on next reset (FR-016). Progress reported as SSE/polled JSON percentage (FR-017).
- **Rationale**: This is the canonical IDF native-OTA pattern; rollback is bootloader-enforced, covering power-loss mid-update (edge case) with no custom code.
- **Alternatives considered**: `esp_https_ota` pull-from-URL (spec v1 is manual upload; can be added later); secure boot signing (out of scope v1 — local-network threat model handled by HTTPS).

## R10. Private CA and device certificates

- **Decision**: `tools/ca/` OpenSSL scripts (PowerShell + bash variants):
  1. `ca-create` — one-time: EC P-256 CA key + self-signed CA cert (~20 y), kept only on the owner's machine.
  2. `device-issue` — per device: EC P-256 key + CSR + CA-signed cert (825-day default) with SANs `DNS:weather-XXXX.local` and optional static IP.
  3. `device-provision` — writes `device.crt`/`device.key` into the littlefs `storage` partition image and flashes it (or uploads via management page for re-issue).
  - Device advertises `weather-XXXX.local` via `espressif/mdns`, matching the cert SAN, so browsers with the CA installed get warning-free HTTPS (FR-012/FR-013, SC-005).
- **Rationale**: mDNS name derived from MAC is stable per device — a certificate SAN that never changes; EC keys keep TLS handshakes fast on ESP32 without PSRAM.
- **Alternatives considered**: self-signed per-device certs (per-device browser exceptions, violates FR-013); embedding certs in firmware (cert would change on every OTA and be identical across devices).

## R11. Temperature history storage

- **Decision**: `history` component. RAM ring of the current hour's samples (12 × 5-min samples). Hourly: single append of the hour's records to the current **monthly** file `/storage/history/YYYYMM.bin` (fixed 8-byte records: `uint32 epoch`, `int16 temp_centi_C`, `uint16 flags/CRC-8+pad`; invalid samples stored with a validity flag). Daily at 00:30 local: delete monthly files entirely older than 3 months (purge = `unlink`, one operation — FR-023). Query API streams records (optionally range-filtered) for `GET /api/history` (JSON) and CSV download (FR-024).
- **Rationale**: 26 000 × 8 B ≈ 208 KB — trivially fits the ~9.8 MB littlefs partition; monthly files make the 3-month purge a file deletion instead of a rewrite; one append/hour meets the flash-wear requirement literally; littlefs is power-loss safe (FR-019, US6 power-loss scenario).
- **Alternatives considered**: NVS blobs (not suited to append streams); single flat file (purge requires full rewrite); SPIFFS (no power-loss guarantees, deprecated in practice).

## R12. Task architecture & shared state (FR-018)

- **Decision**: Three explicitly-named FreeRTOS tasks created from `main` per user requirement — `sensor` (prio 5), `display` (prio 4, drives LVGL widget updates under `lvgl_port_lock`; `esp_lvgl_port` additionally runs its own internal LVGL tick/render task), `web_server` (prio 4; `esp_http(s)_server` worker threads run under it). Cross-task communication: one shared `app_state` struct guarded by a mutex + `esp_event` custom events for changes (reading updated, time synced, unit/timezone changed, WiFi state). No task ever blocks on the network: display/sensor read only local state.
- **Rationale**: Meets the user's explicit task list and FR-018 (network stall cannot freeze display); event-driven updates avoid polling.
- **Alternatives considered**: message queues per pair (more plumbing for the same guarantees at this scale).

## R13. Memory & flash budget (no PSRAM, 16 MB flash)

- **Decision & numbers**:
  - Partition table: `nvs` 64 KB, `otadata` 8 KB, `phy_init` 4 KB, `ota_0` 3 MB, `ota_1` 3 MB, `storage` ≈ 9.8 MB.
  - RAM plan (of ~300 KB usable heap): LVGL buffers ~17 KB, LVGL objects ~20 KB, TLS server ~45 KB/session × 2 max, WiFi+lwIP ~60 KB, tasks/stacks ~24 KB, OTA write buffer 4 KB — leaves >80 KB headroom.
  - App image target ≤ 2.5 MB (fits 3 MB slot with growth margin); gzipped web assets ≤ 60 KB.
- **Rationale**: FR-020 requires dual images + everything in 16 MB — satisfied with ~9.8 MB left for history/certs. TLS session cap is the key no-PSRAM lever.
- **Alternatives considered**: 4 MB OTA slots (shrinks storage needlessly; app nowhere near 3 MB).

## R14. Testing approach

- **Decision**: (a) Unity tests in `components/*/test/` for pure logic: history record codec/purge selection, `accept_language_pick()`, timezone table lookup, hotspot-name formatting from MAC; runnable on target (`idf.py -T`) and structured so logic files compile host-side too. (b) `idf.py build` as CI gate. (c) Manual end-to-end validation scripted in quickstart.md (per user story). 
- **Rationale**: Hardware-in-the-loop stories (WiFi, TLS, OTA, display) are validated by the quickstart procedures; unit tests cover the logic that would otherwise only fail in the field.
- **Alternatives considered**: full QEMU emulation (esp32 QEMU lacks the peripherals that matter here: RMT/SPI panel/WiFi).
