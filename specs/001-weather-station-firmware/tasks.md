# Tasks: ESP32 Weather Station Firmware

**Input**: Design documents from `/specs/001-weather-station-firmware/`

**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/ ✓, quickstart.md ✓

**Organization**: Tasks grouped by user story to enable independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel with other [P]-marked tasks (no shared file dependencies)
- **[Story]**: Maps to user story from spec.md (US1–US6)
- Paths are relative to project root (repo root = ESP-IDF project root, scaffolded via `idf.py create-project`)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project scaffold, build configuration, and shared header that all components depend on.

- [X] T001 Create ESP-IDF project scaffold: run `idf.py create-project esp32-weather-station` at repo root, verify `main/CMakeLists.txt`, `main/main.c`, `CMakeLists.txt`, and `sdkconfig` baseline are generated
- [X] T002 Write `partitions.csv` at repo root: nvs 64KB @ 0x9000, otadata 8KB @ 0x19000, phy_init 4KB @ 0x1B000, ota_0 3MB @ 0x20000, ota_1 3MB @ 0x320000, storage (littlefs) ≈9.9MB @ 0x620000
- [X] T003 Write `sdkconfig.defaults` at repo root: set target esp32, flash size 16MB, custom partition table, bootloader rollback enable, LittleFS component, mbedTLS for esp_https_server, LVGL heap/stack tuning
- [X] T004 [P] Update top-level `CMakeLists.txt` to declare `components/` directory and list all component subdirectories
- [X] T005 [P] Write `main/idf_component.yml` with managed component dependencies: `espressif/ds18b20 ^0.4.0`, `espressif/esp_lvgl_port ^2.8.0`, `espressif/button ^3.x`, `espressif/mdns ^1.x`, `joltwallet/littlefs ^1.x`
- [X] T006 Create all component directory skeletons (each with `CMakeLists.txt` and empty `.c`/`.h` pair): `components/settings/`, `components/sensor/`, `components/history/`, `components/display/`, `components/wifi_mgr/`, `components/captive_dns/`, `components/web_server/`
- [X] T007 [P] Create `components/app_ctx/` component (moved from `main/app_ctx.h` to break circular deps): define `app_state_t` struct, `APP_EVENT` base + event ID enum, extern declarations for global app_state and mutex
- [X] T008 [P] Create `.gitignore` at repo root for ESP-IDF: `build/`, `sdkconfig`, `*.bin`, `*.elf`, `*.map`, `managed_components/`, `.vscode/`, `*.log`, `.DS_Store`, `tools/ca/*.key`, `tools/ca/*.crt` (except ca.crt intentionally committed for reference)

**Checkpoint**: `idf.py build` compiles an empty main.c with correct target and partition table.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that ALL user stories depend on. Must complete before any story work begins.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [X] T009 Implement `components/settings/settings.c` + `components/settings/settings.h`: NVS namespace `settings`, read/write functions for tz_name (str), tz_posix (str), time_mode (u8: 0=local/1=UTC), temp_unit (u8: 0=C/1=F); each write is single `nvs_commit`; `settings_apply_timezone()` calls `setenv("TZ", posix, 1); tzset()` to activate DST rules
- [X] T010 Implement timezone lookup table in `components/settings/tz_table.c`: compiled-in array of ~40 `{iana_name, posix_tz_str}` pairs covering UTC, Europe/Kyiv, Europe/Berlin, Europe/Paris, Europe/London, Europe/Moscow, America/New_York, America/Chicago, America/Denver, America/Los_Angeles, Asia/Tokyo, Asia/Shanghai, Australia/Sydney, and ~25 more common zones; `tz_find_posix(const char *iana)` returns POSIX string or NULL
- [X] T011 [P] Implement `mac_to_suffix()` in `components/wifi_mgr/wifi_mgr.c`: calls `esp_efuse_mac_get_default()`, extracts bytes [4] and [5], formats as 4 lowercase hex chars into caller-supplied buffer
- [X] T012 Initialize NVS flash, mount LittleFS `storage` partition at `/storage`, create `/storage/certs/` and `/storage/history/` directories if absent, init `esp_event_loop_create_default()`, init app_state mutex in `main/main.c`
- [X] T013 [P] Write Unity tests in `components/settings/test/test_settings.c`: (a) `tz_find_posix("UTC")` returns `"UTC0"`, (b) `tz_find_posix("Europe/Kyiv")` returns the correct POSIX DST string, (c) `tz_find_posix("Unknown/Zone")` returns NULL, (d) `mac_to_suffix()` formats `{0xAB, 0xCD}` as `"abcd"`, (e) settings round-trip: write time_mode=1, read back =1

**Checkpoint**: Foundation tasks complete; `idf.py build` succeeds; Unity tests pass on target.

---

## Phase 3: User Story 1 — View Time & Temperature on Display (Priority: P1) 🎯 MVP

**Goal**: Device shows live temperature and current local time on the ST7789V screen in landscape within 10 s of boot.

**Independent Test**: Flash device with probe attached; temperature visible in landscape ≤ 10 s; probe warm-up tracked on screen; unavailability shown when probe disconnected; left/right buttons toggle UTC↔local and °C↔°F.

### Implementation

- [X] T014 [US1] Implement `components/sensor/sensor.c` + `components/sensor/sensor.h`: init onewire_bus RMT on GPIO27, create DS18B20 handle via `ds18b20_new_device_from_enumeration()`, sensor task (`sensor_task`, prio 5, 4KB stack): trigger conversion, wait 800ms, call `ds18b20_get_temperature()`, handle CRC error → valid=false; write result to `app_state.reading` under mutex; post READING_UPDATED event every 5 s
- [X] T015 [US1] Implement `components/display/display.c` + `components/display/display.h`: SPI2 bus init (MOSI=19, SCLK=18), `esp_lcd_panel_io_spi_config_t` with CS=5 DC=16, `esp_lcd_new_panel_st7789()` with RST=23; `esp_lcd_panel_reset()`→`init()`→`set_gap()`; LEDC PWM backlight on GPIO4 (100% duty at boot); `esp_lvgl_port` add display (250×135, two 250×17 DMA buffers, landscape rotation, RGB565)
- [X] T016 [P] [US1] Implement LVGL UI layout in `components/display/ui.c` + `ui.h`: create main screen with temperature label (large font), time label (medium font), "no signal" / "---" validity indicators, unit label (°C/°F), time mode label (LOCAL/UTC); all labels initialized with placeholder values; `ui_init()` called once under `lvgl_port_lock`
- [X] T017 [US1] Implement display task in `components/display/display.c` (`display_task`, prio 4, 6KB stack): subscribe to APP_EVENT for READING_UPDATED + TIME_SYNCED + SETTINGS_CHANGED; on event, acquire `lvgl_port_lock`, update temperature label (apply °C→°F if temp_unit=F, round to 0.1), update time label (`localtime_r` or UTC depending on time_mode), update validity/mode labels; release lock; render loop driven by esp_lvgl_port internal timer
- [X] T018 [US1] Implement `components/display/buttons.c`: init `iot_button` for GPIO0 (BUTTON_ACTIVE_LOW, internal pull-up) and GPIO35 (BUTTON_ACTIVE_LOW, no pull — relies on board); single-click left → read current time_mode, toggle, `settings_set_time_mode()` + emit SETTINGS_CHANGED; single-click right → toggle temp_unit similarly; long-press (5 s) left → `esp_wifi_restore()` + `nvs_flash_erase()` + `esp_restart()` (factory reset)
- [X] T019 [US1] Start `sensor_task` and `display_task` from `main/main.c` after foundation init; store task handles for diagnostics

**Checkpoint**: US1 fully functional — probe temperature and real-time clock visible on screen in landscape; buttons toggle mode/unit and survive reboot.

---

## Phase 4: User Story 2 — First-Time Network Setup via Captive Portal (Priority: P2)

**Goal**: Unconfigured device advertises `weather-XXXX` open hotspot; phone auto-opens a localized portal page (en/de/fr/uk); owner submits SSID+password+timezone; device joins home network and stops the hotspot.

**Independent Test**: Fresh/reset device → hotspot visible → phone portal auto-opens → submit correct credentials → device joins network; wrong credentials → failure feedback + retry; Accept-Language: de → German page.

### Implementation

- [X] T020 [US2] Implement `components/wifi_mgr/wifi_mgr.c` AP-mode path: `esp_wifi_set_storage(WIFI_STORAGE_FLASH)`, read saved SSID via `esp_wifi_get_config()`; if empty SSID → `WIFI_MODE_APSTA`, AP config {ssid=`weather-XXXX`, authmode=OPEN, max_connection=4, ip=192.168.4.1}; emit WIFI_STATE_CHANGED(PROVISIONING_AP); start AP+DHCP server
- [X] T021 [US2] Implement `components/captive_dns/captive_dns.c` + `captive_dns.h`: UDP socket on 0.0.0.0:53, parse incoming DNS A-query name, respond with A record pointing to 192.168.4.1 regardless of queried name; `captive_dns_start()` / `captive_dns_stop()`
- [X] T022 [US2] Implement `components/web_server/i18n.c` + `i18n.h`: `accept_language_pick(header, supported[], n)` → index (0=en fallback); parse comma-separated tags, strip subtags (e.g. `de-AT`→`de`), apply q-value weighting; match against `{"en","de","fr","uk"}`
- [X] T023 [P] [US2] Create i18n string tables in `components/web_server/www/i18n/en.json`, `de.json`, `fr.json`, `uk.json`: keys for all portal UI strings (labels, placeholders, error/success messages, timezone selector label, submit button text)
- [X] T024 [P] [US2] Create portal page assets in `components/web_server/www/portal/index.html`, `portal.css`, `portal.js`: SSID text input + WiFi scan picker (populated via GET /api/scan), password input, timezone `<select>` (populated via GET /api/timezones), submit button; JS: on load fetch scan+timezones, on submit POST /api/wifi then poll GET /api/wifi/status, show spinner + result; string keys referenced from loaded i18n JSON
- [X] T025 [US2] Implement portal HTTP server in `components/web_server/portal_server.c`: `esp_http_server` on :80; handlers: captive probe URLs → 302 to `http://192.168.4.1/`; GET / → serve portal HTML with `Content-Language`, injecting chosen i18n JSON; GET /api/scan → trigger scan + JSON response; GET /api/timezones → array from tz_table; POST /api/wifi → validate body, call `esp_wifi_set_config(STA)`, initiate STA connect, return 202; GET /api/wifi/status → connection state JSON
- [X] T026 [US2] Implement STA join attempt from portal: on successful `IP_EVENT_STA_GOT_IP` after portal submit → `settings_set_timezone(tz_name)`, stop captive_dns, stop AP, emit WIFI_STATE_CHANGED(CONNECTED); on connect failure after timeout → emit WIFI_STATE_CHANGED(AP_FALLBACK), `GET /api/wifi/status` returns `{"state":"failed",...}`
- [X] T027 [P] [US2] Wire portal asset files into `components/web_server/CMakeLists.txt` via `EMBED_FILES` (or build-time gzip + embed); serve with `Content-Encoding: gzip` when compressed
- [X] T028 [P] [US2] Write Unity tests for Accept-Language parser in `components/web_server/test/test_i18n.c`: (a) `Accept-Language: de` → de, (b) `Accept-Language: fr-FR,fr;q=0.9,en;q=0.8` → fr, (c) `Accept-Language: zh-TW` → en (fallback), (d) `Accept-Language: uk,en;q=0.5` → uk, (e) empty header → en

**Checkpoint**: US2 independently functional — hotspot appears, portal localizes, WiFi join succeeds and hotspot stops.

---

## Phase 5: User Story 3 — Automatic Reconnection (Priority: P3)

**Goal**: Device with saved credentials auto-connects on boot; reconnects after transient network loss; falls back to hotspot after sustained unavailability.

**Independent Test**: Power-cycle configured device → joins network < 60 s; disable router → device eventually shows setup hotspot while temperature display continues.

### Implementation

- [X] T029 [US3] Implement STA connect with retry in `components/wifi_mgr/wifi_mgr.c`: on boot with non-empty SSID → `WIFI_MODE_APSTA`→STA connect; handle `WIFI_EVENT_STA_DISCONNECTED`: increment retry counter, exponential backoff (1→2→4→8→16 s), max 6 retries (~63 s total); on exhaustion → emit WIFI_STATE_CHANGED(AP_FALLBACK) + start AP fallback; on `IP_EVENT_STA_GOT_IP` → reset counter, emit WIFI_STATE_CHANGED(CONNECTED)
- [X] T030 [US3] Integrate SNTP in `components/wifi_mgr/wifi_mgr.c`: on `IP_EVENT_STA_GOT_IP` → `esp_sntp_setservername(0, "pool.ntp.org")` + `esp_sntp_init()`; register SNTP sync callback → emit TIME_SYNCED event; call `settings_apply_timezone()` after each sync to keep DST rules current
- [X] T031 [US3] Integrate mDNS in `components/wifi_mgr/wifi_mgr.c`: on STA connected → `mdns_init()` + `mdns_hostname_set("weather-XXXX")` + `mdns_instance_name_set("ESP32 Weather Station")`; `mdns_free()` on disconnect
- [X] T032 [US3] Start `web_server_task` from `main/main.c` (prio 4, 8KB stack); subscribe to WIFI_STATE_CHANGED: on PROVISIONING_AP/AP_FALLBACK → start portal server + captive_dns; on CONNECTED → stop portal server + captive_dns, start HTTPS management server (if cert available)

**Checkpoint**: US3 functional — auto-connect on boot, reconnect after drop, hotspot fallback after sustained loss.

---

## Phase 6: User Story 4 — Secure HTTPS Management Page (Priority: P4)

**Goal**: HTTPS management page at `https://weather-XXXX.local` with owner CA trust chain; cert provisioning tooling for new devices.

**Independent Test**: With CA cert installed on client, `https://weather-XXXX.local` opens without warning; current temperature and device status shown; config changes persist.

### Implementation

- [X] T033 [US4] Load TLS credentials in `components/web_server/web_server.c`: mount LittleFS (already done in main), open `/storage/certs/device.crt` and `/storage/certs/device.key`, read into heap buffers; if either absent → log ESP_LOGW "HTTPS unavailable: device not provisioned" and skip management server start
- [X] T034 [US4] Implement HTTPS management server in `components/web_server/mgmt_server.c`: `esp_https_server_config_t` with loaded cert/key, port 443, max 2 connections; register all management handlers; also start plain :80 listener returning 301 `Location: https://weather-XXXX.local{path}`
- [X] T035 [P] [US4] Create management page assets in `components/web_server/www/mgmt/index.html`, `mgmt.css`, `mgmt.js`: sections — current readings (temp + validity, time + sync state), WiFi status block, device info (FW version, uptime), timezone `<select>` + save, OTA upload form (progress bar), history section (table + CSV download link); data polled from /api/status every 5 s
- [X] T036 [US4] Implement GET /api/status handler in `components/web_server/handlers_mgmt.c`: read app_state under mutex, read settings from NVS, format JSON per contracts/http-api.md
- [X] T037 [US4] Implement GET /api/timezones handler in `components/web_server/handlers_common.c` (shared by portal and management): iterate tz_table, return JSON array of `{"name":"..."}` objects
- [X] T038 [US4] Implement PUT /api/config handler in `components/web_server/handlers_mgmt.c`: parse JSON body subset (tz_name/time_mode/temp_unit), validate each field, call settings setters, emit SETTINGS_CHANGED; return 200 with new effective config or 400 with error envelope
- [X] T039 [US4] Create `tools/ca/ca-create.ps1` and `tools/ca/ca-create.sh`: generate EC P-256 CA key (`ca.key`) and self-signed CA cert (`ca.crt`, 20 years, CA:TRUE, key usage: keyCertSign+cRLSign); print instructions for installing ca.crt on clients
- [X] T040 [US4] Create `tools/ca/device-issue.ps1` and `tools/ca/device-issue.sh`: accept `--suffix XXXX` arg; generate EC P-256 device key, CSR with CN=`weather-XXXX`, sign with CA to produce cert with SAN `DNS:weather-XXXX.local` (825-day validity); output `device.crt` + `device.key`
- [X] T041 [US4] Create `tools/ca/device-provision.ps1` and `tools/ca/device-provision.sh`: build littlefs partition image containing `certs/device.crt` and `certs/device.key` (preserving any existing `history/`); flash image to `storage` partition via `esptool.py`; optionally accept already-running device address for cert re-issue upload via PUT /api/certs (out-of-band, simple alternative)

**Checkpoint**: US4 functional — HTTPS management page loads without cert warning (with CA installed); status/config endpoints work; CA tooling generates certs.

---

## Phase 7: User Story 5 — OTA Firmware Update with Rollback (Priority: P5)

**Goal**: Owner uploads firmware image via management page; device reboots into new version; failed/interrupted updates roll back automatically.

**Independent Test**: Upload valid new image → device reboots, /api/status shows new fw_version; upload a non-firmware file → 400, device unchanged; simulate mid-upload power-loss → device boots previous firmware.

### Implementation

- [X] T042 [US5] Implement `components/web_server/handlers_ota.c`: `UpdateSession` state machine (idle/receiving/validating/applied_pending_reboot/failed) with mutex; POST /api/ota: reject if session not idle (409), reject if Content-Length > 3MB (507); call `esp_ota_begin()`, stream body in 4KB chunks via `esp_ota_write()`, on completion `esp_ota_end()` (validates image); on end success: check `esp_app_desc_t` magic + project name match; `esp_ota_set_boot_partition()` + schedule `esp_restart()` after 3 s delay; set state=applied_pending_reboot; return 200; on any error: state=failed, return 400 with error envelope
- [X] T043 [US5] Implement GET /api/ota/status handler in `components/web_server/handlers_ota.c`: return current UpdateSession JSON (state, progress_pct, error) per contracts/http-api.md
- [X] T044 [US5] Implement post-reboot self-check in `main/main.c`: after all tasks start successfully (sensor reading received, display up, NVS accessible) → call `esp_ota_mark_app_valid_cancel_rollback()`; if startup fails before this call, bootloader will roll back to previous partition on next reset
- [X] T045 [P] [US5] Add OTA progress UI to management page (`components/web_server/www/mgmt/mgmt.js`): file input triggers POST with `XMLHttpRequest` for progress events; poll GET /api/ota/status after upload completes; show reboot countdown; reload page after device comes back up

**Checkpoint**: US5 functional — valid OTA update applies and survives; rollback fires on failed self-check.

---

## Phase 8: User Story 6 — Temperature History (Priority: P6)

**Goal**: 3-month rolling history of 5-minute readings stored to flash; accessible via management page JSON/CSV.

**Independent Test**: Run 2+ hours, history shows 5-min-spaced records; JSON/CSV endpoints return matching data; power-cycle preserves flushed records; 3-month purge removes only out-of-window monthly files.

### Implementation

- [X] T046 [US6] Implement `components/history/history.c` + `components/history/history.h`: RAM ring buffer (12 `HistoryRecord` slots); `history_record_sample()` stores current reading + epoch; hourly flush: open `/storage/history/YYYYMM.bin` for append, write all buffered records (8 bytes each: u32 epoch LE + i16 temp_centi LE + u8 flags + u8 CRC-8 poly 0x07), clear ring; expose `history_record_count()` and `history_query(from, to, callback)` streaming iterator over monthly files
- [X] T047 [US6] Implement daily purge in `components/history/history.c`: `history_purge_old()` — get current epoch, iterate `/storage/history/` directory, parse YYYYMM filename, compute end-of-month epoch, if end-of-month < now − 3 months → `unlink()`; called once per day at 00:30 local (timer callback in history component)
- [X] T048 [US6] Wire history sampling into sensor event flow in `components/history/history.c`: subscribe to READING_UPDATED event; if valid reading and 5-min interval elapsed since last sample → `history_record_sample()`; schedule hourly flush and daily purge via esp_timer
- [X] T049 [US6] Implement GET /api/history handler in `components/web_server/handlers_mgmt.c`: parse optional `from`/`to` epoch query params; call `history_query()` streaming iterator; write chunked JSON response array; omit invalid-flag records from output
- [X] T050 [US6] Implement GET /api/history.csv handler in `components/web_server/handlers_mgmt.c`: same query path, output `text/csv` with header `timestamp_iso8601,temperature_c`, format each record's epoch as ISO-8601 UTC string, temperature as decimal with 2 dp; set `Content-Disposition: attachment; filename="history.csv"`
- [X] T051 [US6] Update GET /api/status to include `history_records` count (call `history_record_count()`) and `storage_free_kb` (LittleFS `statvfs`) in `components/web_server/handlers_mgmt.c`
- [X] T052 [P] [US6] Write Unity tests in `components/history/test/test_history.c`: (a) encode a record, decode bytes, verify CRC-8, (b) record with valid=false has bit0=0 in flags, (c) purge selection: mock YYYYMM pairs, verify correct set marked for deletion given a fixed `now`, (d) ring buffer wraps at 12 entries without data loss for older-than-current-window

**Checkpoint**: US6 functional — 5-min history accumulates, hourly flushes persist, daily purge keeps 3-month window, JSON/CSV endpoints return correct data.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Build verification, security audit, and integration validation across all stories.

- [ ] T053 Verify flash and RAM budget: run `idf.py size-components`, confirm app image ≤ 2.5 MB (fits 3 MB OTA slot); run device and check free heap at idle ≥ 80 KB (`esp_get_free_heap_size()` logged at startup); document numbers in a comment in `main/main.c`
- [ ] T054 [P] Security audit: review all HTTP/HTTPS handlers and confirm (a) `device.key` bytes never appear in any response body, (b) WiFi password never returned by any endpoint, (c) portal server handlers are registered only while in AP mode and management handlers only in STA mode; fix any violations
- [ ] T055 [P] Audit all persistent writes for power-loss atomicity: NVS settings (single `nvs_commit` per logical change ✓), LittleFS history appends (confirm partial tail < 8 B is skipped by CRC check at read time), OTA (bootloader rollback ✓); fix any gaps
- [ ] T056 [P] Build-time asset compression: update `components/web_server/CMakeLists.txt` to gzip portal and management HTML/CSS/JS before embedding; add `Content-Encoding: gzip` and `Content-Type` headers to static file handlers
- [ ] T057 [P] Run quickstart.md end-to-end validation on hardware: all 6 user-story test sequences; record pass/fail; fix regressions
- [ ] T058 [P] Add `components/wifi_mgr/test/test_wifi_mgr.c` Unity test for `mac_to_suffix()`: verify all-zeros MAC → `"0000"`, `{0xAB,0xCD}` → `"abcd"`, `{0xFF,0x00}` → `"ff00"`

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately; T004/T005/T007/T008 can run in parallel after T001
- **Phase 2 (Foundational)**: Depends on Phase 1 complete — BLOCKS all user stories; T011/T013 can run in parallel
- **Phase 3 (US1)**: Depends on Phase 2; T016/T018/T019/T021 can run in parallel once T015 is done
- **Phase 4 (US2)**: Depends on Phase 2; T023/T024/T027/T028 can run in parallel
- **Phase 5 (US3)**: Depends on Phase 4 (builds on wifi_mgr from US2)
- **Phase 6 (US4)**: Depends on Phase 5 (needs STA connected); T035/T039/T040/T041 can run in parallel
- **Phase 7 (US5)**: Depends on Phase 6 (needs HTTPS management server)
- **Phase 8 (US6)**: Depends on Phase 2 (LittleFS mount) and Phase 3 (READING_UPDATED event); T052 can run in parallel
- **Phase 9 (Polish)**: Depends on all prior phases; all [P] tasks can run in parallel

### User Story Dependencies

- **US1 (P1)**: After Phase 2 — sensor + display + buttons, no network needed
- **US2 (P2)**: After Phase 2 — wifi_mgr AP path + portal server + captive DNS
- **US3 (P3)**: After US2 — STA retry + SNTP + mDNS layers on top of wifi_mgr
- **US4 (P4)**: After US3 — HTTPS server needs STA connectivity + cert provisioning tooling
- **US5 (P5)**: After US4 — OTA endpoint on management HTTPS server
- **US6 (P6)**: After Phase 2 (LittleFS) + US1 (READING_UPDATED) — independent of US2–US5

### Within Each Phase

- Component `.c` + `.h` before callers
- Data structures (settings, app_state) before event emitters
- Event emitters before event consumers
- Server handler registration after server init

---

## Parallel Opportunities

```
# Phase 1 — after T001:
T004, T005, T007, T008  (different files, no deps)

# Phase 2 — after T009/T010:
T011, T013              (wifi helper + tests, independent)

# Phase 3 (US1) — after T014+T015:
T016, T018, T019, T021  (ui layout, buttons, backlight, unit conv)

# Phase 4 (US2) — after T020:
T023, T024, T027, T028  (i18n strings, portal assets, cmake embed, tests)

# Phase 6 (US4) — after T033+T034:
T035, T039, T040, T041  (mgmt page assets, CA scripts x3)

# Phase 9 (Polish):
T054, T055, T056, T057, T058  (all independent audit/test/compress tasks)
```

---

## Implementation Strategy

### MVP (User Story 1 only — standalone display)

1. Phase 1: Setup
2. Phase 2: Foundational (settings, tz_table, LittleFS, app_state)
3. Phase 3: US1 (sensor + display + buttons)
4. **STOP and VALIDATE**: Device shows temperature + time, buttons work, survives reboot → SC-001/SC-002 met

### Incremental Delivery

1. Setup + Foundational → buildable project skeleton
2. US1 → standalone temperature/time display (SC-001/SC-002/SC-008) — **MVP**
3. US2 → WiFi onboarding via portal (SC-003)
4. US3 → auto-reconnect + SNTP (SC-004/SC-007 partial)
5. US4 → HTTPS management page + CA tooling (SC-005)
6. US5 → OTA with rollback (SC-006)
7. US6 → temperature history (SC-009)
8. Phase 9 → budget/security/compression/quickstart validation

---

## Notes

- All paths use repo root as project root (ESP-IDF project at top level per `idf.py create-project`)
- [P] = different files, no sequential dependency on another incomplete task in the same phase
- [USN] label maps every task to its user story for traceability
- US1 is intentionally network-free — temperature display works even without WiFi credentials
- `device.key` and WiFi passwords must never be readable via any API (enforced in T054)
- LittleFS mount in Phase 2 (T012) is a prerequisite for both cert loading (US4, T033) and history (US6, T046)
- All Unity tests run via `idf.py -T components/<name> build flash monitor`
