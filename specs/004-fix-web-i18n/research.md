# Research: Fix Web Page Internationalization

**Feature**: 004-fix-web-i18n | **Date**: 2026-07-12

## Root-cause analysis (why i18n is broken today)

| # | Defect | Evidence |
|---|--------|----------|
| 1 | Chosen language never reaches the page. `portal_server.c:65` claims "the JS reads the lang from the data-lang attribute we set via a tiny inline script", but no such script or attribute exists in `www/portal/index.html`; `portal.js:4` therefore always resolves `'en'`. | `portal_server.c:56-77`, `index.html:2`, `portal.js:4` |
| 2 | Language packs are unreachable. All four `www/i18n/*.json` files are embedded (`CMakeLists.txt:18-21`) but neither server registers any `/i18n/*` route, so `portal.js:7`'s fetch always fails and is swallowed by the catch. | `portal_server.c:244-252`, `handlers_mgmt.c:290-300` |
| 3 | Management page was never localized: hardcoded English in `mgmt.html` and `mgmt.js` (status texts, OTA messages, record counts). | `mgmt.html`, `mgmt.js` |
| 4 | `Accept-Language` is read into a 64-byte buffer; real browsers routinely send longer values (e.g. `uk-UA,uk;q=0.9,en-US;q=0.8,en;q=0.7,ru;q=0.6` plus more entries), and truncation can drop the only supported language. Return value of `httpd_req_get_hdr_value_str` is ignored. | `portal_server.c:59-60` |
| 5 | Misleading artifacts that helped this ship broken: `_gz` symbol names for assets that are embedded raw (no gzip step, no `Content-Encoding`), and unit tests that pass while the feature is 100% broken in a browser (only the parser is tested, never the delivery). | `CMakeLists.txt:14-24`, `portal_server.c:38-43`, `test/test_i18n.c` |

Notably, the hardware emulator (`tools/hw_emulator.py:95,496-500,704-715`) already implements the *intended* design — it injects a lang signal into the HTML and serves `/i18n/<lang>.json` — so the emulator, its tests, and `tools/README.md` encode the target behavior the firmware never got.

## D1: Repair the existing method vs. reimplement

The user explicitly allowed reimplementation ("maybe better reimplement method"). Four candidate architectures were evaluated:

**Decision**: **Option A — repair and harden the server-driven method** (server picks language from `Accept-Language`, patches the page's `lang` attribute, page JS fetches the matching pack and applies strings declaratively).

**Rationale**:
- The surrounding ecosystem already encodes this design: `i18n.c` exists and is unit-tested (Unity), the emulator ports it (`accept_language_pick` in Python) and already serves packs, `tools/test_hw_emulator.py` tests the parser, and spec 003's contracts document the behavior. Option A converges firmware onto behavior that is already specified, emulated, and tested; every other option forces rewrites across emulator, tests, and contracts.
- Server-side selection keeps a single decision point, so the `Content-Language` header, the `<html lang>` attribute, and the strings shown can never disagree (spec edge case: "the page must not announce one language while displaying another").
- Cost is small: one shared pack-serving handler, a 3-chunk page send, ~30 lines of C in the shared helpers.

**Alternatives considered**:

| Option | Description | Why rejected |
|--------|-------------|--------------|
| B — Prebuilt per-language pages | Build step renders 4 HTML variants per page; server picks one by `Accept-Language`. No fetch, no flash of English. | Adds a codegen build step (project has none); 4× embedded HTML; dynamic JS messages still need string packs, so the pack mechanism survives anyway — complexity is added, not removed. |
| C — Inline pack injection | Server splices the chosen language's JSON into the HTML as `window.I18N = {…}` during send. Eliminates the pack fetch and its failure mode. | Requires the server to interleave HTML and JSON chunks per request and keep per-page split offsets; more C state than A for a failure mode the spec already covers gracefully (P3/FR-005). Kept as a future optimization if the extra request proves problematic. |
| D — Pure client-side (`navigator.languages`) | JS resolves the language itself, fetches the pack, sets `document.documentElement.lang`. Deletes the C-side selection path entirely. | Attractive simplification, but: diverges from the emulator/contract/tests that already encode server-side selection; `Content-Language` can no longer be emitted consistently; language resolution becomes untestable from the device side (SC-004 requires header-driven vectors verified end-to-end). Firmware deletion is minor since `i18n.c` is already written and tested. |

## D2: How the chosen language reaches the page

**Decision**: Patch the standard `lang` attribute of `<html lang="en">` at response time. All supported codes are exactly 2 characters, so the handler sends the embedded HTML as three chunks: bytes before the attribute value, the 2-char code, bytes after. Split offset is computed once at server start (or first request) by locating `lang="` in the embedded asset. Page JS reads `document.documentElement.lang`.

**Rationale**: Uses the standard attribute browsers, screen readers, and translation tools already honor — satisfying FR-008 with zero extra markup; no `data-lang` convention to document; no per-request buffer copies (chunked send from flash).

**Alternatives considered**: `data-lang` attribute injection (the original intent, and what the emulator currently does) — rejected because it duplicates what `lang` already means and requires regex-style insertion instead of a fixed-width patch; the emulator will be updated to mirror the `lang`-patch instead. `Content-Language` header alone — rejected: page JS cannot read the navigation response's headers. Inline `<script>` setting a variable — rejected: variable-width injection, CSP friction on the management page (`default-src 'self'`).

## D3: Language pack delivery

**Decision**: One wildcard route `GET /i18n/*` registered on **both** servers (portal already runs with `httpd_uri_match_wildcard`), backed by a shared handler in `handlers_common.c` that maps the four supported codes to their embedded pack and returns `application/json; charset=utf-8` with `Cache-Control: max-age=3600`; anything else under `/i18n/` → 404. Handler-slot budget is fine (portal 12→14 of 16 with `/i18n.js`; mgmt ~10→12 of 20).

**Rationale**: Matches the URL shape the front-end and emulator already use (`/i18n/<lang>.json`); one handler instead of four per server; caching matches the existing css/js assets.

**Alternatives considered**: Four explicit routes per server (8 slots, repetitive); serving packs only from the portal server (management page must work on the home network without the portal server running).

## D4: Front-end application mechanism

**Decision**: Reimplement string application declaratively. Translatable elements carry `data-i18n="key"` (text content) or `data-i18n-placeholder="key"` (input placeholders). A new shared script `www/common/i18n.js`, served as `/i18n.js` by both servers and loaded before each page's own script, exposes:
- `i18nReady` — promise resolving to a lookup function `t(key)`;
- resolution: read `document.documentElement.lang`; if `en`, skip the network fetch entirely (English is already in the markup and `t()` falls back to it); otherwise fetch `/i18n/<lang>.json`;
- application: walk `[data-i18n]` / `[data-i18n-placeholder]` and swap text; per-key fallback: missing key → element keeps its English markup text (FR-006);
- any fetch/parse failure → resolve with an empty pack, page stays fully English and functional (FR-005).

**Rationale**: Removes the hand-maintained per-id `setText` list in `portal.js` (a second inventory that can drift); the same 30-line applier serves both pages; keys become greppable from HTML, which the consistency checker (D8) exploits. Skipping the fetch for English removes the extra request for the most common case.

**Alternatives considered**: Keep per-id imperative calls and copy them into `mgmt.js` — two drifting inventories, exactly the maintenance pattern that broke this feature. Inline the applier into both JS files — duplicates logic across files served by different servers.

## D5: Pack structure and key naming

**Decision**: Keep flat JSON packs, one file per language covering **both** pages. Existing portal keys keep their current names (translations already exist in all four languages — zero churn); new management-page keys are prefixed `mgmt_` (e.g. `mgmt_heading_readings`, `mgmt_ota_failed`, `mgmt_records`). `en.json` is the canonical inventory; all packs must carry identical key sets (FR-007).

**Rationale**: Both servers already embed all packs, so a shared file costs nothing extra; flat keys keep the applier and checker trivial; prefixing separates page domains without introducing nesting.

**Alternatives considered**: Per-page packs (`portal-de.json`, `mgmt-de.json`) — halves pack size per page but doubles files, routes, and checker surface for ≤ ~3 KB packs; not worth it. Nested JSON — needs a path-walking applier for no benefit at this scale.

## D6: Localizing dynamic strings (both pages)

**Decision**: All strings composed at runtime (`Scanning…`, OTA progress/failure, `N records`, `no sync`, time-source labels, WiFi status) go through `t(key)` with English literal fallback, e.g. `` `${s.history_records ?? 0} ${t('mgmt_records')}` ``. Count-bearing strings use a `{n}`-style placeholder convention in packs (`"mgmt_records": "{n} records"`) substituted by a tiny helper, avoiding word-order assumptions across languages.

**Rationale**: Ukrainian and German do not share English word order; simple concatenation would produce broken sentences (FR-001/FR-004 cover dynamic content explicitly).

**Alternatives considered**: Full plural-rules support (Ukrainian has 3 plural forms: "1 запис / 2 записи / 5 записів") — deliberate simplification: the UI shows counts as `{n} записів`-style numeric labels where a single form reads acceptably; full CLDR plural logic is out of proportion for 2 count-strings on an embedded device. Documented as a known limitation in data-model.md.

## D7: Accept-Language read hardening

**Decision**: Read the header via `httpd_req_get_hdr_value_len()` first, cap at 255 chars into a 256-byte stack buffer, and treat any read error as "no header" (→ English). Shared helper in `handlers_common.c` so both page handlers behave identically. `i18n.c` already caps its internal working copy at 256 — buffer sizes now aligned end to end.

**Rationale**: Fixes defect #4; deterministic behavior for pathological headers; no heap allocation on the request path.

## D8: String-inventory consistency check (FR-007 / SC-006)

**Decision**: New `tools/check_i18n.py`, runnable standalone (CI/build) and invoked as a pytest case from `tools/test_hw_emulator.py`. Checks:
1. all four packs contain **identical key sets** (diff reported per language);
2. every `data-i18n`/`data-i18n-placeholder` key referenced in `index.html` and `mgmt.html` exists in `en.json`;
3. every `t('key')` reference in `portal.js`/`mgmt.js`/`i18n.js` exists in `en.json`;
4. unused pack keys reported as warnings (not failures).

**Rationale**: This class of defect (inventory drift, dead keys, missing translations) is exactly what shipped broken; a mechanical check makes SC-006 a zero-discrepancy gate.

**Alternatives considered**: CMake-time check — harder to run on dev machines without an IDF build; pytest already runs in CI for the emulator.

## D9: End-to-end verification strategy (SC-001…SC-005)

**Decision**: Three layers:
1. **Unity** (existing, unchanged): `accept_language_pick` parser vectors on target.
2. **pytest + emulator** (new): real HTTP page loads against `hw_emulator.py` asserting (a) `<html lang="xx">` in the response for each SC-004 header vector (`de-AT`, `fr-FR,fr;q=0.9,en;q=0.8`, `uk,en;q=0.5`, `ja, zh;q=0.9, uk;q=0.2`, empty, absent), (b) `Content-Language` agrees with the patched attribute, (c) `/i18n/<lang>.json` serves 200/JSON for all four languages and 404 otherwise, on **both** emulated servers, (d) inventory checker passes (D8).
3. **Manual on-device** (quickstart.md): browser-language matrix walk of portal setup flow and management page, plus the deliberately-broken-delivery scenario (SC-005) exercised via the emulator's fault switch (`SERVE_I18N = False`).

**Rationale**: The old test suite proved the parser while the feature was dead in every browser; layer 2 tests the full HTTP-visible contract so that regression is impossible without a test failure.

## D10: Management-page translation scope (Help section)

**Decision**: Localize the entire management page including the Help card. `mgmt.html` prose is restructured so each translatable sentence/fragment lives in its own element carrying `data-i18n`; non-translatable tokens (button glyphs `←`/`→`, product/OS names, `<code>ca.crt</code>`, links, copyright line) stay as fixed markup outside translatable elements. OS menu paths (e.g. *Settings → Security*) are translated as part of the sentence strings.

**Rationale**: FR-004/SC-002 require 100% of visible text; keeping markup structure in HTML and only text in packs preserves the CSP (`default-src 'self'`, no `innerHTML` needed for packs) and keeps translator-facing strings plain.

**Alternatives considered**: Excluding the Help card from scope — would fail SC-002's "100% of visible text". Allowing HTML inside pack strings + `innerHTML` — larger XSS/CSP surface and harder consistency checking.

## Known accepted limitations

- **Flash of English** (~one network round-trip) on non-English first loads before strings are applied — inherent to Option A; Option C noted as future mitigation if it matters in practice.
- **No plural rules** — count strings use numeric-label phrasing acceptable in all four languages (D6).
- **`_gz` symbol misnomer** — corrected opportunistically in files already being modified; a project-wide rename/gzip decision is out of scope.

All NEEDS CLARIFICATION items: none remained after the trade study (the spec contained none; Technical Context has no unknowns).
