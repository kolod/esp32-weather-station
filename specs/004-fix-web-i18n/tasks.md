# Tasks: Fix Web Page Internationalization

**Input**: Design documents from `/specs/004-fix-web-i18n/`

**Prerequisites**: plan.md, spec.md, research.md (decisions D1–D10), data-model.md, contracts/i18n-http.md, quickstart.md

**Tests**: Included — the spec makes automated verification a first-class requirement (SC-004 end-to-end header vectors, SC-006 zero-discrepancy inventory check), and research D9 defines the test layers.

**Organization**: Tasks are grouped by user story. US1 (portal) is the MVP; US2 (management page) and US3 (graceful fallback) build on the same foundation.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: US1 = portal localized, US2 = management page localized, US3 = graceful fallback

## Phase 1: Setup

**Purpose**: Confirm a green baseline before touching anything.

- [X] T001 Run existing test suites to record the baseline: `python -m pytest tools/test_hw_emulator.py -v` (must pass) and note current portal/mgmt behavior per research.md root-cause table — no code changes.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared mechanisms both pages and both servers need (research D2–D4, D7). No story works without these.

**⚠️ CRITICAL**: Complete before any user story phase.

- [X] T002 [P] Create shared front-end applier `components/web_server/www/common/i18n.js`: expose `i18nReady` promise resolving to `t(key)` and `tn(key, n)` (`{n}` substitution); read `document.documentElement.lang`; skip fetch entirely when `en`; else `fetch('/i18n/<lang>.json')`; on any fetch/parse failure resolve with empty pack; apply pack to all `[data-i18n]` (textContent) and `[data-i18n-placeholder]` (placeholder) elements, leaving elements whose key is missing untouched (FR-005/FR-006, data-model "Translatable element" rules).
- [X] T003 Add hardened Accept-Language helper to `components/web_server/handlers_common.c` + declaration in `handlers_common.h`: use `httpd_req_get_hdr_value_len()` then read into a 256-byte stack buffer (cap 255 chars), treat any error as absent header, call `accept_language_pick()`, return the 2-char code (research D7).
- [X] T004 Add shared language-pack route handler to `components/web_server/handlers_common.c/.h`: wildcard-registered handler for `GET /i18n/*` mapping `en|de|fr|uk` to the embedded `www/i18n/*.json` symbols; respond `application/json; charset=utf-8` + `Cache-Control: max-age=3600`; anything else under `/i18n/` → 404 without echoing the path (contract §2). Include a registration helper callable by both servers. Depends on T003 (same files).
- [X] T005 Add lang-patched page sender to `components/web_server/handlers_common.c/.h`: given embedded HTML start/end and a 2-char code, locate the `lang="` attribute value offset (computed once, cached per asset), send prefix / code / suffix as three chunks; set `Content-Type: text/html; charset=utf-8`, `Content-Language: <code>`, `Cache-Control: no-cache` (contract §1, research D2). Depends on T004 (same files).
- [X] T006 [P] Embed the shared applier and add an `/i18n.js` route: add `"www/common/i18n.js"` to `EMBED_FILES` in `components/web_server/CMakeLists.txt`, and a small shared asset handler for `GET /i18n.js` (`application/javascript`, `max-age=3600`, contract §3) in `components/web_server/handlers_common.c/.h` (coordinate with T005 if same file — do after T005).

**Checkpoint**: Shared C helpers compile; `i18n.js` exists and is embedded. No route is live yet on either server.

---

## Phase 3: User Story 1 — Setup portal appears in the user's language (Priority: P1) 🎯 MVP

**Goal**: Portal page renders 100% in en/de/fr/uk chosen from `Accept-Language`, including dynamic scan/connect status messages (restores 001/FR-022).

**Independent Test**: quickstart.md §2.1–2.3 + §2.5 (portal rows) against the emulator; pytest contract vectors of contracts/i18n-http.md §1–§3 on the portal server.

### Implementation for User Story 1

- [X] T007 [P] [US1] Annotate `components/web_server/www/portal/index.html`: add `data-i18n` attributes for `title`, `heading`, `label_ssid`, `label_password`, `label_timezone`, `btn_scan`, `btn_submit` and `data-i18n-placeholder` for `placeholder_ssid`, `placeholder_password` (existing pack keys, data-model inventory); add `<script src="/i18n.js"></script>` before the `portal.js` script tag; authored English text must exactly match `en.json` values.
- [X] T008 [US1] Rewrite `components/web_server/www/portal/portal.js`: delete the `dataset.lang` read, the manual pack fetch, and the per-id `setText` list; await `i18nReady` from the shared applier and use `t('scanning')`, `t('no_networks')`, `t('secure')`, `t('open')`, `t('status_*')` with the current English literals as fallbacks for all dynamic messages. Depends on T002, T007.
- [X] T009 [US1] Update `components/web_server/portal_server.c`: `portal_page` uses the hardened header helper (T003) + lang-patched sender (T005), delete the stale `data-lang` comment block (lines 65–68); register the `/i18n/*` pack route (T004) and `/i18n.js` route (T006) — handler slots go 12→14 of 16. Depends on T003–T006.
- [X] T010 [P] [US1] Update `tools/hw_emulator.py` portal handler: replace the `data-lang` regex injection in `h_index` (~line 704) with the `lang="…"` attribute patch mirroring firmware behavior (contract §1 + migration notes); serve `/i18n.js` from `www/common/i18n.js` via the shared static-file path.
- [X] T011 [US1] Add portal E2E tests to `tools/test_hw_emulator.py`: for every contract §1 vector (`de`, `de-AT,en;q=0.8`, `fr-FR,fr;q=0.9,en;q=0.8`, `uk,en;q=0.5`, `ja, zh;q=0.9, uk;q=0.2`, `zh-TW`, absent, empty, >255-char header) assert response contains `<html lang="xx"` AND `Content-Language: xx` agree; assert `GET /i18n/{en,de,fr,uk}.json` → 200 JSON and `/i18n/xx.json`, `/i18n/../secret` → 404; assert `GET /i18n.js` → 200 JS. Depends on T010.
- [ ] T012 [US1] Validate story independently: run `python -m pytest tools/test_hw_emulator.py -v`, then quickstart.md §2 portal rows (2.1, 2.2, 2.3, 2.5, 2.6) in a real browser against the emulator; record results in the PR/commit notes.

**Checkpoint**: Portal fully localized end-to-end — shippable MVP.

---

## Phase 4: User Story 2 — Management page appears in the user's language (Priority: P2)

**Goal**: Management page (readings, config, OTA, history, Help card) localized in the same four languages with the same selection behavior.

**Independent Test**: quickstart.md §2.4–2.6 against the emulator's mgmt server; pytest contract vectors against the mgmt server.

### Implementation for User Story 2

- [X] T013 [US2] Annotate `components/web_server/www/mgmt/mgmt.html`: restructure the Help card so each translatable sentence is its own element (glyphs `←`/`→`, OS names as tokens, `ca.crt`, links, copyright stay outside — research D10); add `data-i18n` attributes across all cards using the `mgmt_*` key names from data-model.md; add `<script src="/i18n.js"></script>` before `mgmt.js`; keep CSP-compatible text-only elements (no child elements inside `data-i18n` nodes).
- [X] T014 [US2] Add all `mgmt_*` keys with English values to `components/web_server/www/i18n/en.json`, matching the authored text in mgmt.html exactly (en.json is the canonical inventory, FR-007); include `{n}` placeholder strings (`mgmt_records`). Depends on T013 (final key list comes from the annotated markup).
- [X] T015 [P] [US2] Translate all new `mgmt_*` keys into `components/web_server/www/i18n/de.json`, `fr.json`, and `uk.json` (identical key sets, same `{n}` placeholders, no empty values — data-model validation rules). Depends on T014; the three files are independent of each other.
- [X] T016 [US2] Localize dynamic strings in `components/web_server/www/mgmt/mgmt.js`: await `i18nReady`; route `no sync`, `UTC`/`LOCAL` badges, time-source labels, WiFi status, `{n} records` (via `tn`), OTA progress/success/failure/connection-error strings through `t()` with current English literals as fallbacks. Depends on T002, T014.
- [X] T017 [US2] Update `components/web_server/handlers_mgmt.c`: `mgmt_page` uses hardened header helper + lang-patched sender (existing security headers unchanged); register `/i18n/*` and `/i18n.js` routes on the HTTPS server (slots ~10→12 of 20). Depends on T003–T006.
- [X] T018 [US2] Update `tools/hw_emulator.py` mgmt page handler: apply the same `lang="…"` patch to the served `mgmt.html` (pack serving on the mgmt emulator already works via `BaseEmuHandler.SERVE_I18N`); ensure `/i18n.js` is served on the mgmt emulator too.
- [X] T019 [US2] Add mgmt-server E2E tests to `tools/test_hw_emulator.py`: repeat the contract §1 vectors and §2/§3 route assertions against the emulated management server. Depends on T018.
- [ ] T020 [US2] Validate story independently: pytest suite green, then quickstart.md §2.4–2.6 in a browser (Ukrainian full-page walk incl. Help card, OTA + history dynamic strings, German/Ukrainian layout check at phone width).

**Checkpoint**: Both pages localized; US1 unaffected (portal tests still green).

---

## Phase 5: User Story 3 — Graceful behavior when translations cannot be delivered (Priority: P3)

**Goal**: Any pack-delivery or pack-content failure leaves a complete, functional English page — never blanks or raw keys.

**Independent Test**: quickstart.md §3 fault injection (`SERVE_I18N = False`) — full portal setup flow completes in English.

### Implementation for User Story 3

- [X] T021 [US3] Harden failure paths in `components/web_server/www/common/i18n.js`: non-2xx response, network error, JSON parse error, non-object payload, and slow responses must all resolve `i18nReady` with an empty pack (page stays English and interactive — never a rejected promise or an uncaught error that blocks `portal.js`/`mgmt.js`); verify every dynamic call site in `portal.js`/`mgmt.js` has an English literal fallback (FR-005/FR-006).
- [X] T022 [US3] Add fault-injection tests to `tools/test_hw_emulator.py`: with `SERVE_I18N` patched to `False`, assert page responses are still 200 with a patched `lang` attribute while `/i18n/*.json` returns 404 on both servers (server side of SC-005); with one key deleted from a copied `de.json` fixture, assert the applier contract (missing key → element untouched) via a unit-style check of pack application if feasible, otherwise document as manual step.
- [ ] T023 [US3] Validate story: quickstart.md §3 in a browser — German browser, `SERVE_I18N = False`, complete the entire setup flow (scan → submit → poll) in English with zero broken controls; missing-key experiment shows single-element English fallback. Revert emulator flag afterwards.

**Checkpoint**: All three stories independently validated.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T024 [P] Create `tools/check_i18n.py` (research D8, SC-006): fail on (1) key-set differences between the four packs, (2) `data-i18n`/`data-i18n-placeholder` keys in `index.html`/`mgmt.html` missing from `en.json`, (3) `t('…')`/`tn('…')` keys in `portal.js`/`mgmt.js`/`i18n.js` missing from `en.json`, (4) empty values or `{n}` placeholder mismatches; warn on unused pack keys; exit non-zero on failures; runnable standalone from repo root.
- [X] T025 Add a pytest case in `tools/test_hw_emulator.py` that invokes `tools/check_i18n.py` and asserts zero discrepancies (wires SC-006 into CI). Depends on T024.
- [X] T026 [P] Update `tools/README.md`: replace the `data-lang` injection description (line ~48) with the `lang`-attribute patch + `/i18n.js` behavior, reference `specs/004-fix-web-i18n/contracts/i18n-http.md`.
- [X] T027 [P] Update `specs/003-hardware-emulator/contracts/portal-api.md` (and `mgmt-api.md` if it mentions i18n): note the `data-lang` mechanism is superseded by `specs/004-fix-web-i18n/contracts/i18n-http.md` (migration notes section).
- [X] T028 Rename the misleading `_gz` asset symbols to `_raw`-accurate names in the files already touched (`components/web_server/portal_server.c`, `handlers_mgmt.c`) and fix any comment claiming gzip; do NOT introduce actual gzip (out of scope, research "known limitations").
- [X] T029 Firmware gate: `idf.py build` succeeds; run the `web_server` Unity component tests (`components/web_server/test/test_i18n.c` — unchanged) and confirm green.
- [ ] T030 Full validation pass per quickstart.md §1–§4, including on-device portal (AP mode, phone set to German) and management page (STA mode) checks and the `curl` header spot-check; confirm every SC row in the quickstart pass-criteria table.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: none.
- **Foundational (Phase 2)**: after T001. Blocks all stories. Internal order: T002 ∥ (T003 → T004 → T005 → T006).
- **US1 (Phase 3)**: after Phase 2. T007 ∥ T010 first; T008 after T002+T007; T009 after T003–T006; T011 after T010; T012 last.
- **US2 (Phase 4)**: after Phase 2 (independent of US1, but shares no files with it except `test_hw_emulator.py` — coordinate T011/T019 if run in parallel). Internal order: T013 → T014 → (T015 ∥ T016) ; T017 after T003–T006; T018 → T019; T020 last.
- **US3 (Phase 5)**: after Phase 2; meaningful validation needs at least US1 done (fault test exercises the portal flow). T021 → T022 → T023.
- **Polish (Phase 6)**: T024 → T025 after US1+US2 markup/packs exist; T026–T028 anytime after the code they document/rename is final; T029–T030 last.

### Parallel Opportunities

- Phase 2: T002 (JS) alongside the T003→T005 C chain; T006 right after.
- US1: T007 (HTML) ∥ T010 (emulator) while T009 (C) proceeds.
- US2: T015 translations are three independent files; T013/T014 (markup+en pack) ∥ T017 (C) ∥ T018 (emulator).
- Cross-story: US1 and US2 can be developed in parallel by two people after Phase 2 (distinct files; merge point is `test_hw_emulator.py`).
- Polish: T024, T026, T027 in parallel.

### Parallel Example: User Story 1

```text
# After Phase 2 completes, launch together:
Task: "Annotate index.html with data-i18n attributes"          (T007)
Task: "Update emulator portal handler to lang-attribute patch" (T010)
Task: "Update portal_server.c handlers and routes"             (T009)
# Then: T008 (portal.js) → T011 (pytest) → T012 (validate)
```

---

## Implementation Strategy

### MVP First (User Story 1 only)

1. Phase 1 → Phase 2 → Phase 3.
2. **STOP and VALIDATE** (T012): portal fully localized against the emulator — this alone closes the original FR-022 regression and is shippable.

### Incremental Delivery

1. Foundation (T001–T006).
2. US1 → validate → ship (MVP: portal fixed).
3. US2 → validate → ship (management page localized).
4. US3 → validate (fault-tolerance proven).
5. Polish (T024–T030) → checker wired into CI, docs consistent, on-device pass.

### Notes

- `handlers_common.c/.h` is the single merge hotspot in Phase 2 — keep T003→T005 sequential.
- Commit per task or logical group; keep `python -m pytest tools/test_hw_emulator.py` green from T011 onward.
- Translation quality for T015: reuse the tone/register of the existing portal translations in each pack.
