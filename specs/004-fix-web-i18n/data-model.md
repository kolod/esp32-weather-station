# Data Model: Fix Web Page Internationalization

**Feature**: 004-fix-web-i18n | **Date**: 2026-07-12

## Entity: Language pack

One JSON document per supported language, embedded in the firmware image and served at `/i18n/<code>.json` (see [contracts/i18n-http.md](./contracts/i18n-http.md)).

| Attribute | Value |
|-----------|-------|
| Files | `components/web_server/www/i18n/{en,de,fr,uk}.json` |
| Format | Flat JSON object: `{"key": "translated string", …}` — no nesting, no arrays |
| Encoding | UTF-8 |
| Canonical inventory | `en.json` — every key present here MUST be present in all other packs, and vice versa (FR-007) |
| Size budget | ≤ ~3 KB per pack (soft; keeps the extra request negligible) |

### Validation rules

1. **Identical key sets** across all four packs (checked by `tools/check_i18n.py`; SC-006).
2. **No markup** in values — plain text only; structure lives in HTML (research D10).
3. **Placeholder convention**: count/variable-bearing strings use `{n}` (e.g. `"mgmt_records": "{n} records"`); every pack's value for such a key MUST contain the same placeholders as `en.json`'s value.
4. **No empty values** — a key with an empty string is a checker failure (would render a blank element, violating FR-006's spirit).

### Key inventory

**Existing portal keys** (names unchanged; translations already exist in all 4 packs):

`title`, `heading`, `label_ssid`, `placeholder_ssid`, `label_password`, `placeholder_password`, `label_timezone`, `btn_scan`, `btn_submit`, `status_connecting`, `status_success`, `status_failed_auth`, `status_failed_not_found`, `status_failed_generic`, `scanning`, `no_networks`, `secure`, `open`

**New management-page keys** (prefix `mgmt_`; exact list finalized during implementation when `mgmt.html` markup is annotated — representative inventory):

- Headings/labels: `mgmt_heading_readings`, `mgmt_heading_config`, `mgmt_heading_ota`, `mgmt_heading_history`, `mgmt_heading_help`, `mgmt_label_timezone`, `mgmt_btn_save`, `mgmt_btn_upload`, `mgmt_btn_load_hist`, `mgmt_btn_csv`, `mgmt_th_time`, `mgmt_th_temperature`
- Dynamic status: `mgmt_time_sync_utc`, `mgmt_time_sync_local`, `mgmt_time_no_sync`, `mgmt_time_source_*`, `mgmt_wifi_*`, `mgmt_records` (`{n}`), `mgmt_ota_applied`, `mgmt_ota_failed`, `mgmt_ota_conn_error`
- Help card: `mgmt_help_buttons_heading`, `mgmt_help_btn_left`, `mgmt_help_btn_right`, `mgmt_help_btn_reset`, `mgmt_help_ca_heading`, `mgmt_help_ca_step1..3`, `mgmt_help_ca_windows`, `mgmt_help_ca_macos`, `mgmt_help_ca_android`, `mgmt_help_ca_firefox`

Non-translatable (stay in markup): button glyphs (`←`, `→`), units (`°C`, `°F`), product/OS names as bare tokens, `ca.crt`, URLs, copyright/license line.

## Entity: Language preference → selected language

| Attribute | Value |
|-----------|-------|
| Input | `Accept-Language` request header (may be absent, empty, or up to 255 chars considered; longer is truncated safely — research D7) |
| Selector | `accept_language_pick()` in `components/web_server/i18n.c` (unchanged logic) |
| Supported codes | `en`, `de`, `fr`, `uk` (2-char, lowercase) |
| Rules | Primary-subtag match (`de-AT` → `de`); highest q-value among supported wins; ties keep first-seen; no match / no header / read error → `en` |

### State/consistency invariant

For any page response, these three MUST agree (spec edge case, FR-008):
1. `Content-Language` response header,
2. patched `<html lang="…">` attribute,
3. the pack the page's JS will fetch (derived from #2).

The design guarantees this structurally: one selection is made per request and is the single source for #1 and #2, and #3 is derived from #2 in the browser.

## Entity: Translatable element (HTML annotation)

| Attribute | Value |
|-----------|-------|
| `data-i18n="<key>"` | Element whose text content is replaced by the pack value for `<key>` |
| `data-i18n-placeholder="<key>"` | Input whose `placeholder` attribute is replaced |
| Fallback | Element's authored English content (must always be present in markup — FR-005/FR-006) |

### Validation rules

1. Every referenced key exists in `en.json` (checker rule 2).
2. Every element's authored text is the exact `en.json` value for its key (checker warning — keeps English fallback and English pack from drifting).
3. Elements with `data-i18n` contain no child elements (text-only swap; keeps CSP-safe `textContent` application).

## Client-side lookup function `t(key)`

Provided by `www/common/i18n.js` after `i18nReady` resolves.

| Case | Result |
|------|--------|
| Key in loaded pack | Translated string |
| Key missing from pack / pack failed to load / lang is `en` | English literal supplied at call site (JS) or authored markup text (HTML) |
| Count strings | `t(key).replace('{n}', value)` via helper `tn(key, n)` |

Known limitation (accepted, research D6): no plural-form rules; count strings are phrased as numeric labels valid in all four languages.
