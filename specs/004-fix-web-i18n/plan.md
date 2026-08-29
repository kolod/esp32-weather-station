# Implementation Plan: Fix Web Page Internationalization

**Branch**: `004-fix-web-i18n` | **Date**: 2026-07-12 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/004-fix-web-i18n/spec.md`

## Summary

The captive portal is specified (001, FR-022) to render in en/de/fr/uk chosen from the browser's `Accept-Language`, but two links in the chain were never implemented in firmware: the chosen language is never communicated to the page (the promised `data-lang` injection does not exist in `index.html`), and the embedded `/i18n/<lang>.json` packs have no HTTP route on either server. The management page has no localization at all.

Approach: **repair and harden the existing server-driven method** rather than replace it. The server keeps picking the language from `Accept-Language` (existing, unit-tested `i18n.c`); the page handler patches the standard `<html lang="…">` attribute during send (3-chunk response, all codes are 2 chars); a single wildcard route serves the embedded language packs on both servers; the front-end is reimplemented declaratively (`data-i18n` attributes + one shared applier script) and extended to the management page; a string-inventory consistency check and emulator-backed end-to-end tests close the gap that let this ship broken. Full trade study of repair vs. reimplement options in [research.md](./research.md).

## Technical Context

**Language/Version**: C (C17) on ESP-IDF v5.4.x (firmware); vanilla JavaScript (ES2017+), HTML5, CSS (embedded web UI); Python 3.11+ (hardware emulator + tests)

**Primary Dependencies**: `esp_http_server` (portal, HTTP :80), `esp_https_server` (management, HTTPS :443), assets embedded via `EMBED_FILES` in `components/web_server/CMakeLists.txt`. No new dependencies.

**Storage**: N/A — language packs are compiled into the firmware image (already embedded today).

**Testing**: Unity component tests (`components/web_server/test/test_i18n.c`), pytest against the hardware emulator (`tools/test_hw_emulator.py`), plus a new i18n inventory-consistency check; manual on-device validation per [quickstart.md](./quickstart.md).

**Target Platform**: ESP32 (firmware); evergreen mobile/desktop browsers (UI); Windows/Linux dev host for emulator tests.

**Project Type**: Embedded firmware with embedded web UI (single ESP-IDF project, per-concern components).

**Performance Goals**: Page fully localized on first load; at most one extra request per page (language pack ≤ ~3 KB); no full-page RAM copies in handlers (chunked send).

**Constraints**: `max_uri_handlers` budget (portal 16, currently 12 used; mgmt 20, currently ~10 used); `Accept-Language` header must be handled up to 256 bytes (current 64-byte buffer truncates real-world headers); management server heap is capped (`max_open_sockets = 2`); assets are embedded raw (the `_gz` symbol names are historical — no gzip step exists).

**Scale/Scope**: 4 languages × ~50 strings; 2 pages (portal, management); 2 HTTP servers; emulator mirrors both.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

`.specify/memory/constitution.md` is an unfilled template — no ratified project principles exist, so no project-specific gates apply. General engineering gates applied in their place:

| Gate | Status | Notes |
|------|--------|-------|
| No new dependencies without justification | PASS | Zero new firmware/JS/Python dependencies. |
| No new build steps / codegen | PASS | Rejected the prebuilt-pages option partly for this reason (see research.md D1). |
| Existing tests keep passing; new behavior gets tests | PASS | `test_i18n.c` unchanged and still valid; new pytest E2E + inventory checks added. |
| Simplicity: fix root cause, don't patch symptoms | PASS | Root causes (missing route, missing lang signal) are each fixed by design; misleading comments/symbol claims corrected. |

**Post-design re-check (after Phase 1)**: PASS — design adds one route per server, one shared JS file, one Python checker; no speculative abstraction.

## Project Structure

### Documentation (this feature)

```text
specs/004-fix-web-i18n/
├── plan.md              # This file
├── research.md          # Phase 0: repair-vs-reimplement trade study, decisions D1–D10
├── data-model.md        # Phase 1: language pack schema, key inventory, selection rules
├── quickstart.md        # Phase 1: emulator + on-device validation guide
├── contracts/
│   └── i18n-http.md     # Phase 1: HTTP contract for localized pages + pack delivery
└── tasks.md             # Phase 2 (/speckit-tasks — not created by this command)
```

### Source Code (repository root)

```text
components/web_server/
├── i18n.c                     # MODIFY: no logic change; keep as single source of language selection
├── i18n.h                     # unchanged public contract
├── portal_server.c            # MODIFY: fix portal_page (lang patch via chunked send, drop stale
│                              #         comment), harden Accept-Language read, add /i18n/* route,
│                              #         add /i18n.js route
├── handlers_mgmt.c            # MODIFY: mgmt_page gains same lang patch; register /i18n/* + /i18n.js
├── handlers_common.c/.h       # MODIFY: shared helpers — lang-patched page sender, pack handler,
│                              #         hardened header read (used by both servers)
├── CMakeLists.txt             # MODIFY: embed www/common/i18n.js; rename embedded symbols only if touched
├── test/test_i18n.c           # unchanged (still covers accept_language_pick)
└── www/
    ├── common/
    │   └── i18n.js            # NEW: shared loader/applier (resolve lang → fetch pack → apply
    │                          #      data-i18n/data-i18n-placeholder → set <html lang> stays server-set)
    ├── i18n/{en,de,fr,uk}.json # MODIFY: extend with mgmt_* keys; en.json is the reference inventory
    ├── portal/index.html      # MODIFY: add data-i18n attributes; load /i18n.js before portal.js
    ├── portal/portal.js       # MODIFY: drop per-id setText + dataset.lang; use shared applier + t()
    ├── mgmt/mgmt.html         # MODIFY: add data-i18n attributes (restructure Help prose into
    │                          #         translatable elements); load /i18n.js before mgmt.js
    └── mgmt/mgmt.js           # MODIFY: localize dynamic strings via t() with English fallback

tools/
├── hw_emulator.py             # MODIFY: replace data-lang injection with lang-attribute patch (mirror
│                              #         firmware); serve /i18n.js; mgmt handler already serves packs
├── check_i18n.py              # NEW: inventory consistency checker (packs ↔ HTML ↔ JS)
├── test_hw_emulator.py        # MODIFY: E2E lang-resolution tests (SC-004 vectors), pack-serving
│                              #         tests, invoke checker as a test
└── README.md                  # MODIFY: update i18n behavior description
```

**Structure Decision**: All firmware changes stay inside the existing `web_server` component; shared logic between the two servers goes to `handlers_common.*` (existing shared-helper home). Front-end gains one shared script under `www/common/`; tooling changes stay in `tools/`. No new components, partitions, or build steps.

## Complexity Tracking

No constitution violations — table not required.
