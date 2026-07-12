# Tasks: RTC Time Restore After Reboot

**Input**: Design documents from `/specs/002-rtc-time-restore/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Included — plan.md mandates Unity component tests for the pure logic (validity record, plausibility bounds, state transitions).

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)

## Path Conventions

Single ESP-IDF project at repo root (feature 001 layout): components under `components/`, app entry in `main/`, per-component Unity tests in `components/*/test/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the new component skeleton and pin the build configuration the whole feature relies on.

- [X] T001 Create `rtc_time` component skeleton: `components/rtc_time/CMakeLists.txt` (`idf_component_register` with `PRIV_REQUIRES app_ctx esp_event`; inject `BUILD_EPOCH` compile definition via `string(TIMESTAMP ... "%s" UTC)`), empty-shell `components/rtc_time/rtc_time.h` and `components/rtc_time/rtc_time.c`; confirm `idf.py build` still succeeds
- [X] T002 [P] Pin the ESP-IDF system-time source to "RTC and high-resolution timer" in `sdkconfig.defaults` (verify the exact `CONFIG_` symbol for the project's IDF version via `idf.py menuconfig` — the choice under Component config → Hardware/Newlib system time; e.g. `CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER=y`)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared state, the validity record, and the pure decision logic every user story consumes.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [X] T003 Extend `components/app_ctx/app_ctx.h`: add `app_time_source_t` enum (`APP_TIME_SOURCE_NONE/RTC/NTP`), `time_source` field in `app_state`, and `APP_EVT_TIME_RESTORED` event (keep existing `time_synced` semantics — data-model.md §4)
- [X] T004 Implement validity record and pure decision logic in `components/rtc_time/rtc_time.c`: `RTC_NOINIT_ATTR` record `{magic 0x52544301, version, last_sync_utc, crc32}` per data-model.md §1, `esp_rom_crc32_le` integrity check, `restore_valid(intact, now)` with `BUILD_EPOCH`/`BUILD_EPOCH+20y` bounds (data-model.md §3), and a compile-time `#error` guard on the system-time-source `CONFIG_` macro (contracts/rtc-component-api.md) — depends on T001, T003
- [X] T005 Implement the public API in `components/rtc_time/rtc_time.h` + `rtc_time.c` per contracts/rtc-component-api.md: `rtc_time_restore()` (sets `time_source=RTC`, posts `APP_EVT_TIME_RESTORED`, logs rejection reason at INFO, never blocks/errors), `rtc_time_mark_synced()` (rewrites record CRC-last, sets `time_source=NTP`), `rtc_time_source()`, `rtc_time_last_sync()` — depends on T004
- [X] T006 [P] Unity tests for the pure logic in `components/rtc_time/test/test_rtc_time.c`: record magic/CRC validation (garbage, wrong version, torn write), plausibility bounds (below build epoch, above +20 y, boundary values), state transitions `NONE→RTC→NTP` monotonic (data-model.md §3–4) — depends on T004; parallel with T005

**Checkpoint**: Foundation ready — `idf.py build` passes, unit tests pass, user story phases can begin.

---

## Phase 3: User Story 1 - Correct Time Immediately After Reboot Without Network (Priority: P1) 🎯 MVP

**Goal**: After any reboot with WiFi absent or NTP unreachable, the display shows correct time restored from the battery-backed RTC domain, and history timestamps are valid.

**Independent Test**: Sync once online, kill the network, power-cycle (battery attached) → correct local time on screen within 15 s with no network (quickstart Scenarios A & B).

### Implementation for User Story 1

- [X] T007 [US1] Call `rtc_time_restore()` in `main/main.c` after NVS/event-loop init and **before** display/web_server task creation, so the first frame can show restored time (plan.md Structure Decision)
- [X] T008 [P] [US1] Hook the sync path in `components/wifi_mgr/wifi_mgr.c`: in `sntp_sync_cb()` (after the existing `time_synced = true` / `APP_EVT_TIME_SYNCED` at wifi_mgr.c:100-107) call `rtc_time_mark_synced()` — without this a valid record never exists for restore to find
- [X] T009 [P] [US1] Gate the display's time area in `components/display/display.c` on `app_state.time_source != APP_TIME_SOURCE_NONE` instead of `time_synced` (display.c:146-154) and register a handler for `APP_EVT_TIME_RESTORED` alongside the existing `APP_EVT_TIME_SYNCED` registration (display.c:173)
- [X] T010 [P] [US1] Audit `components/history/history.c` (and its callers): ensure readings are recorded with timestamps whenever `time_source != NONE`, not only after NTP sync — spec US1 acceptance 4 / SC-005; if recording is currently gated on `time_synced`, widen the predicate
- [ ] T011 [US1] Hardware validation on the T-Display (battery attached): execute quickstart.md Scenario A (offline power-cycle, 5/5 runs, ≤5 s offset, ≤15 s to display) and Scenario B (WiFi up / NTP blocked) — depends on T007–T010

**Checkpoint**: MVP — reboot without network restores correct time end-to-end.

---

## Phase 4: User Story 2 - Battery-Backed Clock Stays Synchronized With Network Time (Priority: P2)

**Goal**: Every successful SNTP sync refreshes the validity record so the next restore is accurate; network time stays authoritative and corrections apply smoothly.

**Independent Test**: Note `last_sync`, force/await a periodic SNTP poll, confirm it advances; reboot offline and confirm restored time within 5 s of true time (quickstart Scenario C).

### Implementation for User Story 2

- [X] T012 [US2] Configure SNTP smooth sync mode (`sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH)`) in `components/wifi_mgr/wifi_mgr.c` `start_sntp()` so post-restore NTP corrections adjust without a visible time jump (spec edge case "adjusts smoothly"; verify smooth mode still fires `sntp_sync_cb` for record refresh — fall back to step mode with a comment if the offset exceeds smooth-mode limits)
- [X] T013 [P] [US2] Add Unity test in `components/rtc_time/test/test_rtc_time.c`: repeated `rtc_time_mark_synced()` calls keep the record intact and `last_sync_utc` monotonically increasing; state stays `NTP` (data-model.md §4 refresh transition)
- [ ] T014 [US2] Hardware validation: execute quickstart.md Scenario C (record refresh across periodic polls, then offline reboot within 5 s of true time) — depends on T012

**Checkpoint**: US1 + US2 — restore stays accurate across repeated outage/sync cycles.

---

## Phase 5: User Story 3 - Graceful Behavior When the Backup Clock Is Unusable (Priority: P3)

**Goal**: Cold boot with no/dead battery degrades to today's "time not available" behavior with nothing else broken, and the management page reports the time-source state.

**Independent Test**: Battery disconnected, power off ≥30 s, boot without network → "not available" time area, temperature works, `/api/status` shows `"time_source":"none"` (quickstart Scenario D).

### Implementation for User Story 3

- [X] T015 [P] [US3] Verify and harden the rejection path in `components/rtc_time/rtc_time.c`: each failure cause (record corrupt / below build epoch / above upper bound) leaves `time_source = NONE`, logs its distinct reason at INFO, and `rtc_time_last_sync()` returns `-1` on a corrupt record (FR-006; contracts/rtc-component-api.md)
- [X] T016 [US3] Extend `GET /api/status` in `components/web_server/handlers_mgmt.c` (api_status, handlers_mgmt.c:79-149): add `"time_source":"none"|"rtc"|"ntp"` and `"time_last_sync":<unix>|null` per contracts/status-api.md, preserving all existing fields and the `time_synced ⟺ "ntp"` invariant
- [X] T017 [P] [US3] Management page UI in `components/web_server/www/`: render the three time-source states ("Time: not available" / "from backup clock (last synced …)" / "synchronized (last synced …)") with localized strings for en/de/fr/uk (contracts/status-api.md UI note)
- [ ] T018 [US3] Hardware validation: execute quickstart.md Scenario D (battery-removed cold boot: normal boot, temperature works, no wrong time shown, status fields correct; then network reconnect flips to `"ntp"`) — depends on T015–T017

**Checkpoint**: All three user stories independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T019 [P] Full gate: `idf.py build` clean and all Unity tests in `components/rtc_time/test/` pass; fix any warnings introduced by this feature
- [ ] T020 Execute long-running quickstart.md validations: Scenario E (7-day offline drift ≤1 min, SC-002) and Scenario F (offline history timestamps, SC-005) — start early, verify at the end
- [X] T021 [P] Consistency pass: `components/rtc_time/rtc_time.h` doc comments match contracts/rtc-component-api.md; confirm no new FreeRTOS tasks/timers were introduced (plan.md Complexity Tracking); commit history clean

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: none — start immediately; T001 ∥ T002
- **Foundational (Phase 2)**: needs T001 (component exists) and T003 before T004; T005 ∥ T006 after T004 — **blocks all stories**
- **US1 (Phase 3)**: needs Phase 2; T007/T008/T009/T010 are four different files → parallel; T011 last
- **US2 (Phase 4)**: needs Phase 2; independent of US1 code-wise (T012 touches `wifi_mgr.c` like T008 — serialize those two if run concurrently); T014 validation is most meaningful after US1
- **US3 (Phase 5)**: needs Phase 2; independent of US1/US2; T016 before T017's final verification, T018 last
- **Polish (Phase 6)**: after all desired stories

### User Story Dependencies

- **US1 (P1)**: none beyond Foundational — MVP
- **US2 (P2)**: shares `wifi_mgr.c` with US1's T008 (same function) — implement T008 first or together
- **US3 (P3)**: none beyond Foundational; hardware test D is cleanest before long-run Scenario E

### Parallel Opportunities

```text
Phase 1:  T001 ∥ T002
Phase 2:  T003 → T004 → (T005 ∥ T006)
Phase 3:  (T007 ∥ T008 ∥ T009 ∥ T010) → T011
Phase 4:  (T012 ∥ T013) → T14
Phase 5:  (T015 ∥ T016 ∥ T017) → T018
Cross-story: after Phase 2, US1 / US2 / US3 can proceed in parallel
             (only shared file: wifi_mgr.c between T008 and T012)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phase 1 (T001–T002) → Phase 2 (T003–T006) → Phase 3 (T007–T011)
2. **STOP and VALIDATE**: quickstart Scenarios A & B on hardware
3. This alone delivers the headline behavior: correct time after reboot without network

### Incremental Delivery

1. MVP (above)
2. US2 (T012–T014): accuracy stays fresh across outage cycles
3. US3 (T015–T018): failure transparency + management-page status
4. Polish (T019–T021), with the 7-day Scenario E clock started as early as possible

---

## Notes

- Total tasks: 21 — Setup 2, Foundational 4, US1 5, US2 3, US3 4, Polish 3
- Hardware-validation tasks (T011, T014, T018, T020) need the physical T-Display with battery; everything else is host/build work
- Commit after each task or logical group; `idf.py build` must stay green at every checkpoint
