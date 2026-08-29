# Tasks: WiFi Details Screen

**Input**: Design documents from `/specs/008-wifi-details-screen/`

**Prerequisites**: plan.md, spec.md, research.md (D1–D7), data-model.md,
contracts/wifi-details-screen.md, quickstart.md

**Tests**: No new automated tests. Per plan.md Testing strategy, the added code is LVGL overlay
glue plus an `esp_wifi`/`esp_netif` snapshot — neither host-testable with the current framework,
and `components/display` has no host-test target. Verification is the `idf.py build` zero-warning
gate, the unchanged regression suites (`tools/test_hw_emulator.py`, `tools/check_i18n.py`) staying
green, and the on-device `quickstart.md` checks.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2)

## Path Conventions

ESP-IDF component layout per plan.md: firmware in `components/*` and `main/`. No web/tooling
changes in this feature.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Give `display` a build-graph edge to `wifi_mgr` so it can read WiFi facts

- [X] T001 In `components/display/CMakeLists.txt`, add `wifi_mgr` to the `REQUIRES` list (keep the existing `esp_event settings app_ctx esp_wifi nvs_flash`); run `idf.py build` and confirm it still succeeds

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The `wifi_mgr_get_info()` accessor every story reads from — type + connected-mode branch + hostname (research D4, data-model §1, contracts §1)

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T002 In `components/wifi_mgr/wifi_mgr.h`, add `typedef struct { char ssid[33]; char ipv4[16]; char hostname[32]; } wifi_mgr_info_t;` and declare `void wifi_mgr_get_info(wifi_mgr_info_t *out);` with the doc comment from contracts §1 (non-blocking, safe any time, all fields NUL-terminated)
- [X] T003 In `components/wifi_mgr/wifi_mgr.c`, implement `wifi_mgr_get_info()`: zero `*out`; always set `hostname` to `snprintf("weather-%s.local", mac_to_suffix())`; for `s_state == WIFI_ST_CONNECTED` fill `ssid` from `esp_wifi_sta_get_ap_info()` and `ipv4` from `esp_netif_get_ip_info(s_sta_netif, …)` via `esp_ip4addr_ntoa` (`esp_netif.h` already included). *(Implemented the full state switch in one pass — the AP-fallback/connecting branches specified for T008 landed here too; a static `netif_ipv4_str()` helper handles both interfaces.)* Verified `idf.py build` succeeds.

**Checkpoint**: `wifi_mgr_get_info()` returns correct data in connected mode; overlay work can begin

---

## Phase 3: User Story 1 - Check how to reach the device on the network (Priority: P1) 🎯 MVP

**Goal**: Long-press the right button while connected → a full-screen overlay shows SSID, IPv4, and `weather-XXXX.local`; it auto-returns after 10 s or on a second long-press; all other button actions keep working (FR-001, 002, 003, 004, 005, 007, 008, 009, 010, 011, 012).

**Independent Test**: With the device on WiFi, long-press right → overlay shows the joined network name, current IP, and local hostname; wait 10 s → weather view returns; long-press twice → returns immediately; right short-click and both left-button actions still work.

### Implementation for User Story 1

- [X] T004 [US1] In `components/display/ui.h`, add `#include "wifi_mgr.h"` and declare `void ui_show_wifi_details(const wifi_mgr_info_t *info);` and `void ui_hide_wifi_details(void);` with the "caller MUST hold lvgl_port_lock()" contract note (contracts §3)
- [X] T005 [US1] In `components/display/ui.c`, build the overlay in `ui_init()`: one opaque black full-panel `lv_obj` child of `lv_screen_active()`, `LV_OBJ_FLAG_SCROLLABLE` removed, vertical flex, left-aligned, created with `LV_OBJ_FLAG_HIDDEN`; inside it 3 caption labels (`"Network"`, `"IP"`, `"Host"`, grey `FONT_LABEL`) each followed by a value label (white `FONT_LABEL`, `lv_label_set_long_mode(…, LV_LABEL_LONG_DOTS)`, width `LCD_H_RES`); store overlay + 3 value-label handles in statics. Implement `ui_show_wifi_details()` (NULL-guard; set the 3 value labels — empty string → `"---"` per FR-006; clear `LV_OBJ_FLAG_HIDDEN`) and `ui_hide_wifi_details()` (set `LV_OBJ_FLAG_HIDDEN`; no-op if already hidden). Layout per data-model §4 (research D3, D5). *(This LVGL build names the enum `LV_LABEL_LONG_MODE_DOTS`, not `LV_LABEL_LONG_DOTS`.)*
- [X] T006 [US1] In `components/display/buttons.c`, add `#include "ui.h"`, `#include "wifi_mgr.h"`, `#include "esp_lvgl_port.h"`, `#include "esp_timer.h"`; add statics `s_wifi_overlay_shown` (bool) and `s_auto_hide_timer` (`esp_timer_handle_t`); implement `auto_hide_cb()` → `lvgl_port_lock()`, `ui_hide_wifi_details()`, `s_wifi_overlay_shown = false`, `lvgl_port_unlock()`; implement `on_right_long()` → under `lvgl_port_lock()`: if shown → `ui_hide_wifi_details()`, `s_wifi_overlay_shown=false`, `esp_timer_stop(s_auto_hide_timer)`; else → `wifi_mgr_get_info(&info)` (called before taking the lock), `ui_show_wifi_details(&info)`, `s_wifi_overlay_shown=true`, `esp_timer_stop()` then `esp_timer_start_once(s_auto_hide_timer, 10*1000*1000)`; unlock. In `buttons_init()` create the one-shot timer (`esp_timer_create` with `.callback = auto_hide_cb`, `.dispatch_method = ESP_TIMER_TASK`) and register `iot_button_register_cb(right, BUTTON_LONG_PRESS_START, NULL, on_right_long, NULL)` (contracts §2, research D1/D2/D6)
- [~] T007 [US1] Run `idf.py build` (zero warnings) — **DONE, clean**. Flash and run quickstart §3 on hardware *(requires device — not run in this session)*: overlay appears < 1 s after the long-press; three correct labelled lines; 10 s auto-return; second long-press returns immediately; browse to the shown IP and to `weather-XXXX.local` — page loads; right short-click still toggles °C/°F, left click still toggles LOCAL/UTC, left long-press still factory-resets; a sensor sample taken while the overlay is up is reflected after it closes (FR-009, FR-010)

**Checkpoint**: WiFi details screen fully works in connected mode — MVP complete

---

## Phase 4: User Story 2 - Find the device while it is in access-point fallback mode (Priority: P2)

**Goal**: The same overlay shows the broadcast AP name, the portal IP (`192.168.16.1`), and the hostname when the device is in AP fallback; during the connecting phase it shows the SSID being joined with a placeholder IP (FR-002, FR-003, FR-006).

**Independent Test**: Force AP fallback (erase credentials) → long-press right → overlay shows `weather-XXXX`, `192.168.16.1`, `weather-XXXX.local`; during a connect attempt with no IP yet, the IP line shows `---`.

### Implementation for User Story 2

- [X] T008 [US2] In `components/wifi_mgr/wifi_mgr.c`, extend `wifi_mgr_get_info()` with the remaining `s_state` branches (data-model §1 population table): `WIFI_ST_PROVISIONING_AP` / `WIFI_ST_AP_FALLBACK` → `ssid` from `esp_wifi_get_config(WIFI_IF_AP, …).ap.ssid`, `ipv4` from `esp_netif_get_ip_info(s_ap_netif, …)`; `WIFI_ST_CONNECTING` / `WIFI_ST_RETRYING` → `ssid` from `esp_wifi_get_config(WIFI_IF_STA, …).sta.ssid` if non-empty else `""`, `ipv4` stays `""`; `WIFI_ST_IDLE` → both `""`. Verify `idf.py build` succeeds
- [~] T009 [US2] Flash and run quickstart §4 on hardware *(requires device — not run in this session)*: in AP-fallback mode the overlay shows the broadcast SSID, `192.168.16.1`, and the hostname; connect a phone to `weather-XXXX` and open `192.168.16.1` → portal loads (SC-004); enter credentials and during the connecting window long-press right → SSID being joined shown, IP line shows `---` (FR-006)

**Checkpoint**: Overlay is correct in every WiFi state

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Documentation sync and full-feature validation

- [X] T010 [P] In `README.md`, update the Buttons entries (line ~7 feature bullet and line ~24 table row) to add "Right long-press — show WiFi details (network name, IP, `.local` hostname)"
- [X] T011 [P] In `.specify/memory/constitution.md`, update the "Buttons" line under **Hardware Platform Standards** to include the right-button long-press action (documentation sync — no version bump)
- [~] T012 Run the full regression + edge sweep: `idf.py build` (zero warnings) — **DONE**; `python tools/test_hw_emulator.py` (91 tests OK) and `python tools/check_i18n.py` (63 keys × 4 langs, 0 discrepancies) both green and unchanged (SC-005) — **DONE**; quickstart §5 edge checks on hardware — rapid toggling ends in a consistent state, a ~30-char SSID clips with `…` without shifting layout (FR-011), WiFi drop while the overlay is shown never mixes AP + STA data; confirm SC-001–SC-004, SC-006 *(hardware checks — not run in this session)*

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies — start immediately
- **Foundational (Phase 2)**: needs T001; T003 needs T002 — **BLOCKS all user stories**
- **US1 (Phase 3)**: needs Phase 2. T004 first; then T005 and T006 in parallel (different files, both only need T004's declarations, T006 also needs T003); T007 last
- **US2 (Phase 4)**: needs Phase 2 for the accessor and Phase 3 for the overlay/trigger (US2 adds no UI). T008 then T009
- **Polish (Phase 5)**: T010 and T011 anytime after Phase 1; T012 after all desired stories

### Parallel Opportunities

- T005 and T006 (different files: `ui.c` vs `buttons.c`) once T004 lands
- T010 and T011 (different files) anytime
- US2's T008 (`wifi_mgr.c`) could proceed alongside US1's T005/T006 if staffed separately — it only touches `wifi_mgr.c`

## Parallel Example: User Story 1

```bash
# After T004 (ui.h declarations) lands:
Task: "T005 [US1] overlay create + show/hide in components/display/ui.c"
Task: "T006 [US1] right long-press handler + esp_timer in components/display/buttons.c"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phase 1 (T001) → Phase 2 (T002–T003): accessor returns connected-mode data
2. Phase 3 (T004–T007): overlay + long-press + auto-return, verified on hardware in STA mode
3. **STOP and VALIDATE**: quickstart §3 green → demo the MVP

### Incremental Delivery

1. + US2 (T008–T009): AP-fallback + connecting-state coverage → verify quickstart §4
2. Polish (T010–T012): README + constitution doc sync, full regression + edge sweep

---

## Notes

- **Status (2026-08-29)**: all code + docs complete; `idf.py build` clean (zero warnings),
  emulator suite (91 tests) and i18n checker (63×4 keys) green and unchanged. Remaining:
  `T007`, `T009`, and the `quickstart §5` portion of `T012` — on-device validation, needs the
  hardware. Marked `[~]`.
- Total: **12 tasks** (Setup 1, Foundational 2, US1 4, US2 2, Polish 3)
- No new managed component, font, NVS key, GPIO, HTTP endpoint, or i18n key — `check_i18n.py` and the emulator suite must come out **unchanged**
- All overlay-state mutation (`s_wifi_overlay_shown`, timer arm/disarm, `LV_OBJ_FLAG_HIDDEN`) happens under `lvgl_port_lock()` — the button task and esp_timer task cannot interleave (research D6)
- `wifi_mgr_get_info()` is deliberately implemented across T003 (connected branch) and T008 (other states) so US1 and US2 stay independently deliverable
- Commit after each task or logical group; every checkpoint is a valid stopping point
