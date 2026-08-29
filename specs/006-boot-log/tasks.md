# Tasks: Boot Log

**Input**: Design documents from `/specs/006-boot-log/`

**Prerequisites**: plan.md, spec.md, research.md (D1–D7), data-model.md, contracts/bootlog-api.md, quickstart.md

**Tests**: Included — the spec mandates them (SC-004 regression gate; the emulator-coverage assumption) and plan.md's Testing strategy commits to Unity + emulator suites.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)

## Path Conventions

ESP-IDF component layout per plan.md: firmware in `components/*` and `main/`, web assets in `components/web_server/www/`, dev tooling in `tools/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the new component and register it in the build graph

- [X] T001 Create `components/boot_log/` skeleton: `boot_log.h` (public API: `boot_log_init`, `boot_log_close`, `boot_log_get`, plus buffer-logic functions exposed for host tests per plan.md), `boot_log.c` (empty implementations), `CMakeLists.txt` (`idf_component_register(SRCS "boot_log.c" INCLUDE_DIRS "." PRIV_REQUIRES log)`)
- [X] T002 Register the component in the build graph: add `boot_log` to `REQUIRES` in `main/CMakeLists.txt` and in `components/web_server/CMakeLists.txt`; verify `idf.py build` still succeeds

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The capture engine every story depends on — buffer, tee, lifecycle, file flush (research D1/D2/D7, data-model §1–§3)

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 Implement buffer logic as pure, host-testable functions in `components/boot_log/boot_log.c`: line append with all-or-nothing 4096-byte cap and 32-byte reserved tail, one-time `[boot log full — further messages discarded]` truncation marker, tag extraction from `L (millis) tag: message` lines, allowlist prefix match (`sensor`, `bmp280`, `display`, `ds18b20`, `onewire`, `i2c.master`, `lcd_panel`, `spi`), ANSI escape stripping (data-model §1 validation rules)
- [X] T004 Add Unity host test `components/boot_log/test/test_boot_log.c` + `components/boot_log/test/CMakeLists.txt` (same harness as `components/sensor/test/`): cases for append, cap enforcement, no line splitting, truncation marker written once, allowlisted vs non-allowlisted tags, ANSI strip; run and pass
- [X] T005 Implement runtime plumbing in `components/boot_log/boot_log.c`: vprintf tee installed via `esp_log_set_vprintf()` that always forwards to the previous vprintf and appends allowlisted lines under a dedicated FreeRTOS mutex (never calls `ESP_LOG` itself); `boot_log_init()` deletes stale `/storage/boot.log` then installs the hook (state INACTIVE→CAPTURING); `boot_log_close()` stops capture, writes the buffer once to `/storage/boot.log`, silently ignores write failure, retains the RAM buffer (state CAPTURING→CLOSED, data-model §2); `boot_log_get()` returns buffer pointer + length
- [X] T006 Wire the lifecycle in `main/main.c`: call `boot_log_init()` immediately after `esp_vfs_littlefs_register()`/`mkdir` block and before `sensor_start()`; call `boot_log_close()` after the OTA self-check block; verify `idf.py build` succeeds

**Checkpoint**: Capture engine complete — device writes `/storage/boot.log` each boot; user story implementation can now begin

---

## Phase 3: User Story 1 - View boot diagnostics from management page (Priority: P1) 🎯 MVP

**Goal**: A developer opens the management page after boot and reads sensor-detection and display-init diagnostics — no serial cable (FR-001/002/005/007/009).

**Independent Test**: Start the device (or emulator), open the management page, and verify a Boot Log section shows sensor detection and display initialization results from the most recent boot; with the log missing, the section shows a localized "not available" message.

### Implementation for User Story 1

- [X] T007 [P] [US1] In `components/sensor/bmp280.c`, emit `ESP_LOGW(TAG, "BMP280 not found at 0x76 or 0x77")` when `bmp280_detect()` finds no device (spec US1 scenario 3; today only a per-address `ESP_LOGD` exists at line ~120)
- [X] T008 [P] [US1] In `components/display/display.c`, add `ESP_LOGI` progress lines (currently the file logs nothing): SPI bus + panel IO created, ST7789 panel initialized (with resolution), LVGL port ready in `panel_init()`; UI initialized in `display_task()` (research D3; failure paths stay `ESP_ERROR_CHECK` — do not soften)
- [X] T009 [US1] In `components/web_server/handlers_mgmt.c`, add `api_bootlog` handler per contracts/bootlog-api.md: read `/storage/boot.log`, respond `200 text/plain; charset=utf-8` with its content; when unavailable respond `404` with `{"error":"not_found"}`; register `{.uri="/api/boot.log", .method=HTTP_GET, .handler=api_bootlog}` in `register_mgmt_handlers()` and update the hardcoded loop bound `for (int i = 0; i < 7; i++)` → `8` (line ~325)
- [X] T010 [P] [US1] In `components/web_server/www/mgmt/mgmt.html`, add `<section class="card bootlog">` between the History and Help cards: `<h2 data-i18n="mgmt_heading_bootlog">`, `<pre id="bootlog-content">`, and an unavailable-message element with `data-i18n="mgmt_bootlog_unavailable"` (hidden by default) — structure per data-model §4
- [X] T011 [P] [US1] In `components/web_server/www/mgmt/mgmt.css`, style the bootlog card: monospace `<pre>`, capped height with `overflow: auto`, consistent with existing card styling
- [X] T012 [US1] In `components/web_server/www/mgmt/mgmt.js`, on page load `fetch('/api/boot.log')`: on 200 fill `#bootlog-content` with the body verbatim; on non-200 or fetch error hide the `<pre>` and show the localized unavailable message (FR-007 — the page must never fail to render)
- [X] T013 [P] [US1] Add i18n keys `mgmt_heading_bootlog`, `mgmt_btn_bootlog`, `mgmt_bootlog_unavailable` (en values per data-model §5, translated for de/fr/uk) to all four packs in `components/web_server/www/i18n/{en,de,fr,uk}.json`; run `python tools/check_i18n.py` — must pass (FR-009, SC-005)
- [X] T014 [US1] In `tools/hw_emulator.py`, add route `("GET", "/api/boot.log"): "h_bootlog"` to `MgmtHandler.ROUTES`; implement `h_bootlog` serving a synthetic ESP-IDF-format log consistent with the active `sensor` scenario (bmp280 → found lines; ds18b20 → BMP280-not-found + DS18B20-found; none → both-not-found), ≤ 4096 bytes; add scenario field `bootlog: "present"|"missing"` (default present, `missing` → 404 shape) to `/emu/scenario` GET/PUT and a `--bootlog` CLI flag (contracts §Emulator)
- [X] T015 [US1] In `tools/test_hw_emulator.py`, add tests: `GET /api/boot.log` returns 200 + `text/plain` + body ≤ 4096 bytes + content consistent with each `--sensor` kind; `bootlog=missing` scenario returns the 404 `{"error":"not_found"}` shape; mgmt page HTML contains `bootlog-content` and `mgmt_heading_bootlog`; all four i18n packs contain the three new keys
- [X] T016 [US1] Validate US1: run `python tools/test_hw_emulator.py`, `python tools/check_i18n.py`, and `idf.py build` — all green (quickstart §1, §2, §4); manually load the emulator mgmt page in both `present` and `missing` scenarios

**Checkpoint**: Boot log viewable on the management page (emulator-verified, firmware builds) — MVP complete

---

## Phase 4: User Story 2 - Download boot log as a file (Priority: P2)

**Goal**: One-click download of the raw log as `boot.log`, byte-identical to the view (FR-006).

**Independent Test**: Click "Download" in the Boot Log section and verify a plain-text file named `boot.log` is saved whose content equals what the page displays.

### Implementation for User Story 2

- [X] T017 [US2] In `components/web_server/www/mgmt/mgmt.html`, add `<a id="btn-bootlog" href="/api/boot.log" download="boot.log" data-i18n="mgmt_btn_bootlog">` to the bootlog card (styled like the existing `#btn-csv` link); in `components/web_server/www/mgmt/mgmt.js`, hide the link in the unavailable state (contracts §UI)
- [X] T018 [US2] In `tools/test_hw_emulator.py`, add tests: mgmt page HTML contains the `btn-bootlog` anchor with `download="boot.log"`; two consecutive `GET /api/boot.log` responses are byte-identical (view/download identity, US2 scenario 2); run the suite — green

**Checkpoint**: View and download both work and provably serve the same bytes

---

## Phase 5: User Story 3 - Boot log persists through network-up delay (Priority: P3)

**Goal**: Early-boot diagnostics are retrievable once the device comes online, and log-path failures can never break boot (FR-003/008).

**Independent Test**: Power-cycle the device, wait for network + web server, retrieve the boot log and confirm it contains early-boot (sensor/display init) lines with early millis timestamps; storage unavailability does not prevent normal boot.

### Implementation for User Story 3

- [X] T019 [US3] In `components/web_server/handlers_mgmt.c`, extend `api_bootlog` with the RAM fallback per contracts/bootlog-api.md: when `/storage/boot.log` is absent/unreadable, serve the live buffer from `boot_log_get()` (bridges the pre-flush window when a fast STA connect beats the 10 s close point); 404 only when file AND buffer are both empty (`#include "boot_log.h"` — dependency added in T002)
- [X] T020 [US3] Resilience audit of FR-008 in `components/boot_log/boot_log.c` and `main/main.c`: confirm every failure in the log path (stale-file delete, hook install, buffer full, file write) is silently tolerated (serial-only diagnostics allowed), no `ESP_ERROR_CHECK`/abort on any boot-log operation, hook never recurses into `ESP_LOG`; add/adjust Unity cases in `components/boot_log/test/test_boot_log.c` for close-with-empty-buffer and append-after-close (no-op)
- [ ] T021 [US3] On-device validation per quickstart §5 *(requires hardware — everything else is done; run quickstart §5 with the device connected)*: flash, power-cycle without serial, confirm early-boot millis timestamps in the retrieved log (US3 scenario 1); reboot → only newest boot's lines (FR-003); erase-flash negative check → device boots and mgmt page degrades gracefully (US3 scenario 2, FR-008)

**Checkpoint**: All user stories independently functional on hardware

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Documentation and end-to-end validation

- [X] T022 [P] Update `tools/README.md`: document the `/api/boot.log` emulator route, the `bootlog` scenario field, and the `--bootlog` CLI flag
- [ ] T023 Run the full quickstart.md validation end-to-end *(§1 emulator contract checks, §2 automated suites, and §4 build gate PASS; §3 Unity tests build with the firmware — running them and §5 need the device)* (emulator contract checks §1, automated suites §2, host unit tests §3, build gate §4, on-device §5) and confirm SC-001…SC-005; fix anything found

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately; T002 needs T001
- **Foundational (Phase 2)**: Depends on Setup — **BLOCKS all user stories**; order T003 → T004, T003 → T005 → T006 (T004 can run parallel with T005 once T003 lands)
- **User Stories (Phase 3–5)**: All depend on Phase 2
  - **US1 (Phase 3)**: no story dependencies; within it T007/T008/T010/T011/T013 are parallel; T012 after T010; T015 after T014; T016 last
  - **US2 (Phase 4)**: touches the same mgmt.html/mgmt.js/test files as US1 — run after US1 (its i18n key already landed in T013)
  - **US3 (Phase 5)**: T019 extends the T009 handler — run after US1; T020 independent of T019; T021 last (needs hardware)
- **Polish (Phase 6)**: after all desired stories; T022 anytime after T014

### Parallel Opportunities

- **Phase 3 wave 1**: T007, T008, T010, T011, T013 (five different files) + T009 and T014 (different files again) — up to 7 tasks concurrently
- T004 (host test) alongside T005 (runtime plumbing) after T003
- T020 alongside T019; T022 alongside any Phase 5 work

## Parallel Example: User Story 1

```bash
# Wave 1 — all independent files:
Task: "T007 BMP280 not-found warning in components/sensor/bmp280.c"
Task: "T008 display init log lines in components/display/display.c"
Task: "T009 /api/boot.log handler in components/web_server/handlers_mgmt.c"
Task: "T010 bootlog card in components/web_server/www/mgmt/mgmt.html"
Task: "T011 bootlog styles in components/web_server/www/mgmt/mgmt.css"
Task: "T013 i18n keys in components/web_server/www/i18n/*.json"
Task: "T014 emulator route/scenario in tools/hw_emulator.py"

# Wave 2 — dependents:
Task: "T012 fetch/render in components/web_server/www/mgmt/mgmt.js"   # after T010
Task: "T015 contract tests in tools/test_hw_emulator.py"              # after T014
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phase 1 (T001–T002) → Phase 2 (T003–T006): capture engine done, file written each boot
2. Phase 3 (T007–T016): viewable, localized, emulator-tested boot log
3. **STOP and VALIDATE**: quickstart §1–§4 all green → demo the MVP

### Incremental Delivery

1. + US2 (T017–T018): download link → suite green → deliver
2. + US3 (T019–T021): RAM fallback + resilience + on-device proof → deliver
3. Polish (T022–T023): docs + full quickstart sweep

---

## Notes

- Total: **23 tasks** (Setup 2, Foundational 4, US1 10, US2 2, US3 3, Polish 2)
- The `uris[]` loop bound in `register_mgmt_handlers()` is hardcoded (`< 7`) — T009 must bump it or the new route silently never registers
- `mgmt_btn_bootlog` lands with the other keys in T013 (checker parity requires all-at-once) even though its anchor appears in US2
- Commit after each task or logical group; every checkpoint is a valid stopping point
