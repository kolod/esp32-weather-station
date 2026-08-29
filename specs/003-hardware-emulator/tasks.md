# Tasks: Local Hardware Emulator for Web UI Testing

**Input**: Design documents from `/specs/003-hardware-emulator/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Unit tests ARE included — plan.md/research.md R11 explicitly specify a stdlib `unittest` suite for the pure logic (language picker, state machines, generators, validation). No browser automation.

**Organization**: Tasks grouped by user story. Note: the emulator is deliberately a single file (`tools/hw_emulator.py`, stdlib only — user constraint), so tasks within a story are mostly sequential; the parallel opportunities are between the main file and the test file, and between stories once the foundation is done.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: US1 (mgmt page), US2 (portal), US3 (OTA & edge scenarios)

## Path Conventions

- Emulator: `tools/hw_emulator.py` (new)
- Tests: `tools/test_hw_emulator.py` (new)
- Served assets (existing, **never modified**): `components/web_server/www/`

---

## Phase 1: Setup

**Purpose**: Skeleton files and CLI so everything after has a home

- [X] T001 Create `tools/hw_emulator.py` skeleton: module docstring (purpose + usage), `argparse` CLI with all flags from contracts/emulator-control.md (`--mgmt-port` 8080, `--portal-port` 8081, `--no-browser`, `--history-hours` 24, `--join-outcome`, `--ota-outcome`, `--sensor-invalid`, `--time-not-synced`, `--wifi-disconnected`, `--storage-free-kb`), `main()` entry, `WWW_ROOT` resolved relative to script (`../components/web_server/www`) with a clear startup error naming the expected absolute path if missing (spec edge case)
- [X] T002 [P] Create `tools/test_hw_emulator.py` skeleton: stdlib `unittest` scaffolding importing from `tools/hw_emulator.py`, runnable via `python -m unittest tools.test_hw_emulator`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared state, i18n, HTTP plumbing, dual-server startup — required by every story

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 Implement shared state in `tools/hw_emulator.py`: `EmulatorState` and `Scenario` per data-model.md (all fields, defaults, single `threading.Lock`, scenario initialization from parsed CLI args)
- [X] T004 Implement `TIMEZONES` catalog (~25 IANA names incl. `UTC`) and `is_valid_timezone()` in `tools/hw_emulator.py` (single source for `/api/timezones` and config/wifi validation — research R6)
- [X] T005 Implement `accept_language_pick(header)` in `tools/hw_emulator.py`: en/de/fr/uk, q-values, primary-subtag match, `en` fallback — port of `components/web_server/i18n.c` (research R8, data-model Language)
- [X] T006 [P] Add `accept_language_pick` unit tests in `tools/test_hw_emulator.py` mirroring all eight cases from `components/web_server/test/test_i18n.c`
- [X] T007 Implement base HTTP machinery in `tools/hw_emulator.py`: `BaseHTTPRequestHandler` subclass with route-table dispatch (exact path + method), JSON response helper, request logging line per request, `MISS <method> <path>` log + 404 for unknown routes (spec edge case), and a `serve_file()` helper that re-reads from `WWW_ROOT` on every request with correct MIME types and `Cache-Control: no-cache` (research R9, FR-003)
- [X] T008 Implement dual-server startup/shutdown in `tools/hw_emulator.py`: two `ThreadingHTTPServer` instances on `127.0.0.1` (mgmt + portal ports), port-in-use error naming the port and override flag (spec edge case), console banner printing mgmt/portal/control URLs, `webbrowser.open()` of both pages after both sockets bind unless `--no-browser` (FR-013, FR-014, research R10), clean Ctrl-C shutdown of both servers

**Checkpoint**: `python tools/hw_emulator.py` starts, prints URLs, opens browser tabs, serves 404 + logs for any request; unit tests for T005 pass

---

## Phase 3: User Story 1 — Develop the Management UI Without a Device (Priority: P1) 🎯 MVP

**Goal**: Management page fully functional against simulated data with live-edit loop

**Independent Test**: quickstart.md "Validate — User Story 1": open `http://127.0.0.1:8080/`, see populated dashboard; change settings and see them persist; view history + CSV; edit `mgmt.html`, refresh, see the change

- [X] T009 [US1] Implement synthetic sensor + history in `tools/hw_emulator.py`: seeded sinusoid temperature function, startup generation of `--history-hours` at 60 s intervals, lazy append on read, `from`/`to` inclusive filtering (data-model SyntheticSensor & History, research R6)
- [X] T010 [P] [US1] Add generator unit tests in `tools/test_hw_emulator.py`: temperature range/determinism, history ordering, count, from/to filtering
- [X] T011 [US1] Implement mgmt static routes in `tools/hw_emulator.py`: `GET /` (mgmt.html + CSP + nosniff headers, no HSTS), `/mgmt.css`, `/mgmt.js`, `GET /i18n/<lang>.json` on the mgmt port, per contracts/mgmt-api.md
- [X] T012 [US1] Implement `GET /api/status` in `tools/hw_emulator.py`: exact JSON shape from contracts/mgmt-api.md (temperature, time fields, wifi block, fw_version, uptime from `boot_monotonic`, history_records, storage_free_kb), including scenario-driven variants (sensor_valid / time_synced / wifi_connected)
- [X] T013 [US1] Implement `PUT /api/config` in `tools/hw_emulator.py`: flat-JSON parsing of `tz_name`/`time_mode`/`temp_unit`, in-order validation with `unknown_timezone` / `invalid_time_mode` / `invalid_temp_unit` 400 errors, success returns full status body (contracts/mgmt-api.md)
- [X] T014 [P] [US1] Add config validation unit tests in `tools/test_hw_emulator.py`: each error case, partial updates, persistence of applied values
- [X] T015 [US1] Implement `GET /api/history`, `GET /api/history.csv`, and `GET /api/timezones` in `tools/hw_emulator.py`: JSON records shape, CSV with `Content-Disposition` + ISO-8601 Z rows, timezone array with `max-age=3600` (contracts/mgmt-api.md)

**Checkpoint**: Management page fully interactive per quickstart US1 steps 1–5; unit tests pass — this is the MVP

---

## Phase 4: User Story 2 — Exercise the Captive Portal Provisioning Flow (Priority: P2)

**Goal**: Full provisioning journey (scan → join → poll) with reproducible failure paths and language selection

**Independent Test**: quickstart.md "Validate — User Story 2": scan shows networks; `HomeNet` join reaches connected; `Emu-WrongPass` / `Emu-NotFound` show the right failures; probe URLs redirect; browser language switches translations

- [X] T016 [US2] Implement scan catalog + `GET /api/scan` in `tools/hw_emulator.py`: fixed ~8-network list with designated outcome SSIDs and ~1 s artificial delay (data-model ScanNetwork, contracts/portal-api.md)
- [X] T017 [US2] Implement `JoinSession` state machine in `tools/hw_emulator.py`: `connecting` → after ~2 s (injectable clock for tests) → `connected(suffix)` or `failed(reason)`; outcome from designated SSID override else `scenario.join_outcome` (data-model JoinSession)
- [X] T018 [P] [US2] Add JoinSession unit tests in `tools/test_hw_emulator.py`: success/auth/not_found transitions, designated-SSID override, re-attempt restarts session
- [X] T019 [US2] Implement `POST /api/wifi` and `GET /api/wifi/status` in `tools/hw_emulator.py`: body validation (`empty_body`, `invalid_ssid`, `password_too_long`), optional `tz_name` silently applied if valid, 202 `{"status":"connecting"}`, and the exact three status shapes (contracts/portal-api.md)
- [X] T020 [US2] Implement portal static + captive routes in `tools/hw_emulator.py`: five probe URLs → 302 to portal root; `GET /` serving index.html with `Content-Language` and on-the-fly `data-lang` injection into the `<html>` tag; `/portal.css`, `/portal.js`, `/i18n/<lang>.json`, `/api/timezones` on the portal port (contracts/portal-api.md, research R8)

**Checkpoint**: Portal flow end-to-end per quickstart US2 steps 1–6; US1 unaffected

---

## Phase 5: User Story 3 — Simulate Firmware Update and Edge Conditions (Priority: P3)

**Goal**: OTA upload with progress/outcomes, and runtime-switchable edge scenarios

**Independent Test**: quickstart.md "Validate — User Story 3": dummy upload succeeds with visible progress then simulated reboot; `invalid_image` scenario shows page error; `/emu` toggles flip status presentation live

- [X] T021 [US3] Implement `OtaSession` state machine in `tools/hw_emulator.py`: `receiving` with `progress_pct` from bytes streamed, `validating`, outcome per `scenario.ota_outcome`, `applied_pending_reboot` → 3 s → simulated reboot (uptime reset, `fw_version` bump, state `idle`) (data-model OtaSession, research R7)
- [X] T022 [P] [US3] Add OtaSession unit tests in `tools/test_hw_emulator.py`: full success path incl. simulated reboot effects, both failure outcomes, 409-while-receiving guard, size limits
- [X] T023 [US3] Implement `POST /api/ota` and `GET /api/ota/status` in `tools/hw_emulator.py`: chunked body consumption updating progress, 409 `update_in_progress`, 507 `image_too_large` (>3 MB or ≤0), 400 `invalid_image` with message, 500 for `write_error`, success `{"status":"applied","reboot_in_s":3}`; status JSON with `error` null-or-string (contracts/mgmt-api.md)
- [X] T024 [US3] Implement scenario control endpoints in `tools/hw_emulator.py`: `GET /emu/scenario` (full dict), `PUT /emu/scenario` (JSON patch, per-field validation, 400 `{"error":"invalid_scenario","field":...}`), changes affecting subsequent requests only (contracts/emulator-control.md)
- [X] T025 [US3] Implement `GET /emu` control page in `tools/hw_emulator.py`: self-contained inline HTML/JS (no external assets, no framework) rendering scenario fields as selects/checkboxes wired to the JSON endpoint (contracts/emulator-control.md)
- [X] T026 [P] [US3] Add scenario unit tests in `tools/test_hw_emulator.py`: patch validation (good/bad fields), scenario effects on status output (sensor_valid, time_synced, wifi_connected, storage_free_kb)

**Checkpoint**: All three stories independently functional; every CLI flag has a working runtime equivalent

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T027 [P] Create `tools/README.md`: one-paragraph purpose, usage examples (default run, scenario flags, port overrides), pointer to specs/003-hardware-emulator/quickstart.md, and the R8 note that portal i18n is emulated per firmware *intent* (device currently 404s `/i18n/*`)
- [X] T028 Run full unit suite `python -m unittest tools.test_hw_emulator -v` and fix any failures
- [X] T029 Execute quickstart.md end-to-end (all US1/US2/US3 walkthroughs + edge cases: second instance port error, run from foreign cwd, unknown-route MISS log) and record results; verify browser devtools shows zero failed page requests (SC-003)
- [X] T030 Verify `git status` shows no modifications under `components/web_server/www/` (hard constraint: pages stay ESP32-compatible) and final code cleanup pass on `tools/hw_emulator.py` (consistent naming, no dead code, module docstring current)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: none
- **Phase 2 (Foundational)**: needs Phase 1 — **blocks all stories**
- **Phases 3/4/5 (US1/US2/US3)**: each needs only Phase 2; mutually independent (single-file edits are sequential for one person, but stories touch disjoint routes/state and can be validated independently)
- **Phase 6 (Polish)**: needs all desired stories complete

### Within stories

- T009 → T012/T015 (status & history need the generator); T011 anytime after T007
- T017 → T019 (wifi endpoints drive the session); T016 before T019 for designated SSIDs
- T021 → T023; T024 → T025 (control page wraps the endpoint)
- Test tasks ([P], in `tools/test_hw_emulator.py`) can be written alongside their implementation task

### Parallel Opportunities

- T002 ∥ T001; T006 ∥ T007–T008; each story's test tasks (T010, T014, T018, T022, T026) ∥ the next implementation task in `tools/hw_emulator.py`; T027 ∥ T028
- With two people: one drives `tools/hw_emulator.py` story by story, the other writes `tools/test_hw_emulator.py` and runs quickstart validations

---

## Implementation Strategy

**MVP first**: Phases 1–3 only (T001–T015) already deliver the core value — the management page develops against the emulator with a <10 s edit loop. Stop, validate quickstart US1, then decide.

**Incremental delivery**: add Phase 4 (portal provisioning testing), then Phase 5 (OTA + runtime scenario control), validating each checkpoint via its quickstart section before moving on. Phase 6 closes with docs, the full test suite, and the end-to-end walkthrough.
