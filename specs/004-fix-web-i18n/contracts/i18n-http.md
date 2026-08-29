# HTTP Contract: Localized Pages & Language Pack Delivery

**Feature**: 004-fix-web-i18n | **Date**: 2026-07-12

Applies to **both** servers, which MUST behave identically for everything below:
- Portal server — HTTP, port 80 (`portal_server.c`), captive-portal/AP mode
- Management server — HTTPS, port 443 (`mgmt_server.c` + `handlers_mgmt.c`), STA mode

The hardware emulator (`tools/hw_emulator.py`) MUST mirror this contract on both of its emulated servers. This supersedes the `data-lang` injection behavior described in spec 003's contracts (`specs/003-hardware-emulator/contracts/portal-api.md`) — see Migration notes.

## 1. GET `/` — localized page

### Request

| Header | Handling |
|--------|----------|
| `Accept-Language` | Optional. Up to 255 characters considered; longer values are truncated safely (never an error). Absent/empty/unreadable → selection falls back to `en`. |

### Language selection

Per `accept_language_pick()` semantics (single source of truth, `components/web_server/i18n.c`):
primary-subtag match against `{en, de, fr, uk}`, highest q-value wins, fallback `en`.

### Response

| Element | Requirement |
|---------|-------------|
| Status | `200 OK` |
| `Content-Type` | `text/html; charset=utf-8` |
| `Content-Language` | The selected 2-char code |
| `Cache-Control` | `no-cache` (page carries a per-request patch; MUST NOT be cached as language-neutral) |
| Body | The embedded page HTML with the `<html lang="…">` attribute value patched to the selected code. All other bytes identical to the embedded asset. |

**Invariant**: `Content-Language` header value == `lang` attribute value, for every response.

Existing management-page security headers (`Strict-Transport-Security`, `Content-Security-Policy`, `X-Content-Type-Options`) are unchanged by this feature.

### Contract test vectors (SC-004)

| `Accept-Language` sent | Expected `lang` attr / `Content-Language` |
|------------------------|-------------------------------------------|
| `de` | `de` |
| `de-AT,en;q=0.8` | `de` |
| `fr-FR,fr;q=0.9,en;q=0.8` | `fr` |
| `uk,en;q=0.5` | `uk` |
| `ja, zh;q=0.9, uk;q=0.2` | `uk` |
| `zh-TW` | `en` |
| *(header absent)* | `en` |
| *(empty value)* | `en` |
| *(> 255 chars, supported lang within first 255)* | that lang |

## 2. GET `/i18n/{code}.json` — language pack

### Request

`{code}` ∈ `{en, de, fr, uk}`.

### Response (supported code)

| Element | Requirement |
|---------|-------------|
| Status | `200 OK` |
| `Content-Type` | `application/json; charset=utf-8` |
| `Cache-Control` | `max-age=3600` (packs are immutable per firmware version, same policy as css/js) |
| Body | The embedded pack, byte-identical to `www/i18n/{code}.json` |

### Response (anything else under `/i18n/`)

`404 Not Found`. The route MUST NOT echo the requested path into the response body.

## 3. GET `/i18n.js` — shared applier script

| Element | Requirement |
|---------|-------------|
| Status | `200 OK` |
| `Content-Type` | `application/javascript` |
| `Cache-Control` | `max-age=3600` |
| Body | Embedded `www/common/i18n.js` |

Loaded by both pages via `<script src="/i18n.js"></script>` **before** the page's own script.

## 4. Client-side behavior (page ↔ pack)

1. Script reads `document.documentElement.lang`.
2. If `en` → no pack request is made (English markup is authoritative).
3. Otherwise → `GET /i18n/<lang>.json` from the **same origin/server** that served the page.
4. On HTTP error, network error, or JSON parse error → page remains fully English and fully functional; no retry, no user-visible error (FR-005).
5. On success → all `[data-i18n]`/`[data-i18n-placeholder]` elements are updated; keys absent from the pack leave the element's English content untouched (FR-006).

## Migration notes (emulator & docs)

- `hw_emulator.py` replaces its `data-lang="…"` injection (`h_index`, ~line 704) with the `lang`-attribute patch defined in §1; `portal.js` stops reading `dataset.lang`.
- `SERVE_I18N = False` on the emulator remains the supported fault switch for validating FR-005/SC-005 (pack delivery broken → English fallback).
- `tools/README.md` and spec 003 contract references to `data-lang` are updated to reference this contract.
