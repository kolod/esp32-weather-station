# Implementation Plan: Boot Log

**Branch**: `006-boot-log` | **Date**: 2026-07-15 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/006-boot-log/spec.md`

## Summary

Capture the diagnostic messages emitted during sensor detection and display initialization and make them readable from the management web page without a serial cable. A new `boot_log` component installs an `esp_log` vprintf tee (tag-allowlisted to the sensor/display subsystems and their drivers) right after LittleFS mounts, accumulates lines in a static 4 KB RAM buffer, and flushes it once to `/storage/boot.log` when `app_main` finishes its boot sequence — no flash writes in the log path, so a storage failure can never break boot (FR-008). The management server gains `GET /api/boot.log` (plain text, RAM-buffer fallback before the flush) and the management page gains a localized "Boot Log" card with a `<pre>` view and a download link, mirroring the existing history-CSV pattern. The hardware emulator serves a synthetic log at the same endpoint with a scenario switch for the "not available" case, so the whole HTTP/UI contract is testable without hardware.

## Technical Context

**Language/Version**: C (C17, `-std=gnu23` toolchain) on ESP-IDF v6.0.2; vanilla JS/HTML (embedded web UI); Python 3.11+ (emulator/tests)

**Primary Dependencies**: ESP-IDF `log` (`esp_log_set_vprintf` tee) and VFS/LittleFS already mounted at `/storage` by `main`. **No new managed components.**

**Storage**: One plain-text file `/storage/boot.log` on the existing LittleFS partition, ≤ 4096 bytes (SC-003), deleted at capture start and written once per boot (FR-003).

**Testing**: Unity component test for the buffer logic (append, 4 KB cap, truncation marker, tag filter — pure functions); Python unittest suite against the emulator (`/api/boot.log` contract, missing-log scenario, mgmt page section); `tools/check_i18n.py` (3 new keys × 4 languages); `idf.py build` gate; on-device quickstart.

**Target Platform**: ESP32 (LilyGO T-Display class board); evergreen browsers for UI.

**Project Type**: Embedded firmware + embedded web UI + Python dev tooling (existing structure).

**Performance Goals**: Log capture adds only a RAM `memcpy` per allowlisted line (no flash I/O on the sensor/display task paths); `/api/boot.log` served within normal page-load time (SC-002); single ≤ 4 KB flash write per boot.

**Constraints**: 4 KB hard cap, discard-not-wrap on overflow (FR-004); capture window closes at a fixed point in `app_main` (after the 10 s OTA self-check delay) so the file reflects startup only; the vprintf hook must be re-entrant-safe (own mutex), must never call `ESP_LOG` itself, and must always forward to the previous vprintf so serial output is unchanged; boot must succeed regardless of storage state (FR-008).

**Scale/Scope**: 1 new component (~150 lines + test), 1 new HTTP endpoint, 1 new UI card, 3 new i18n keys × 4 languages, 2 call-site touch-ups (sensor/display log lines), emulator route + scenario field.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

`.specify/memory/constitution.md` is an unfilled template — generic engineering gates applied:

| Gate | Status | Notes |
|------|--------|-------|
| No new dependencies without justification | PASS | Uses IDF's own `esp_log` hook + existing LittleFS mount; no managed components added (D1). |
| Existing behavior preserved | PASS | Tee forwards to the previous vprintf — serial logging unchanged; boot sequence order untouched; log-path failures are silent by design (FR-008). |
| Bounded resource use | PASS | Static 4 KB buffer, one file write per boot, file hard-capped at 4 KB (SC-003). |
| New logic gets tests | PASS | Buffer/filter logic in Unity; HTTP + UI contract in emulator suite; i18n parity via checker. |

**Post-design re-check (after Phase 1)**: PASS — one component with a 4-function API, one additive endpoint, no speculative abstraction (no log rotation, no clear-log UI, no portal exposure — all explicitly out of scope per spec assumptions).

## Project Structure

### Documentation (this feature)

```text
specs/006-boot-log/
├── plan.md              # This file
├── research.md          # Phase 0: decisions D1–D7
├── data-model.md        # Phase 1: boot log file format, capture lifecycle, i18n keys
├── quickstart.md        # Phase 1: emulator + on-device validation
├── contracts/
│   └── bootlog-api.md   # Phase 1: GET /api/boot.log + emulator scenario contract
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
components/
├── boot_log/                  # NEW component
│   ├── boot_log.c             # vprintf tee (tag allowlist, own mutex), 4 KB buffer,
│   │                          #   init (delete stale file, install hook),
│   │                          #   close (flush buffer → /storage/boot.log, stop capture),
│   │                          #   read accessor for the web handler
│   ├── boot_log.h             # boot_log_init/close/get API + buffer-logic functions
│   │                          #   exposed for host tests
│   ├── CMakeLists.txt         # PRIV_REQUIRES log
│   └── test/                  # NEW: test_boot_log.c (append/cap/truncation/tag filter)
├── sensor/
│   └── bmp280.c               # MODIFY: ESP_LOGW "BMP280 not found at 0x76 or 0x77"
│                              #   on detect failure (spec US1 scenario 3)
├── display/
│   └── display.c              # MODIFY: add ESP_LOGI progress/success lines to
│                              #   panel_init()/display_task() (currently logs nothing)
└── web_server/
    ├── handlers_mgmt.c        # MODIFY: GET /api/boot.log handler (file, RAM fallback);
    │                          #   uris[] grows 7 → 8 (loop bound!)
    ├── CMakeLists.txt         # MODIFY: REQUIRES boot_log
    └── www/
        ├── mgmt/mgmt.html     # MODIFY: Boot Log card (pre + download link) before Help
        ├── mgmt/mgmt.js       # MODIFY: fetch /api/boot.log → fill pre | unavailable msg
        └── i18n/{en,de,fr,uk}.json  # MODIFY: 3 new mgmt_bootlog_* keys (4 langs)

main/
├── main.c                     # MODIFY: boot_log_init() after LittleFS mount;
│                              #   boot_log_close() after OTA self-check
└── CMakeLists.txt             # MODIFY: REQUIRES boot_log

tools/
├── hw_emulator.py             # MODIFY: ("GET","/api/boot.log") route, synthetic log
│                              #   matching --sensor kind, scenario "bootlog" present|missing
└── test_hw_emulator.py        # MODIFY: contract tests (200 text/plain ≤4096 B, 404 shape,
                               #   mgmt.html section present, i18n keys)
```

**Structure Decision**: `boot_log` is a new leaf component because two unrelated components need it (`main` drives its lifecycle, `web_server` reads it) — it cannot live inside either without creating an awkward dependency. It depends only on IDF `log` and libc stdio (VFS), keeping it host-testable. Everything else extends existing files along the exact patterns already in the codebase: the endpoint follows `/api/history.csv`, the UI card follows the History card, the emulator route follows `h_history_csv`, localization follows the checker-enforced key parity.

## Complexity Tracking

No constitution violations — table not required.
