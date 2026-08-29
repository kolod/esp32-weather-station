# Quickstart: Validating Web Page i18n

**Feature**: 004-fix-web-i18n | **Date**: 2026-07-12

Validation scenarios proving the feature end-to-end. Contract details: [contracts/i18n-http.md](./contracts/i18n-http.md); string inventory: [data-model.md](./data-model.md).

## Prerequisites

- Python 3.11+ (emulator & tests): `python --version`
- For on-device checks: ESP-IDF v5.4.x environment, flashed device
- A browser whose UI language / language list you can change (Chrome: `chrome://settings/languages`; Firefox: `about:preferences#general` → Language)

## 1. Automated checks (no hardware)

```powershell
# From repo root
python -m pytest tools/test_hw_emulator.py -v          # includes new i18n E2E + contract vectors
python tools/check_i18n.py                             # inventory consistency (SC-006)
```

**Expected**: all tests pass; checker reports `0 discrepancies` across the four packs, both HTML files, and all JS `t()` references.

The pytest suite covers the SC-004 header vectors from the contract (§1) against real HTTP page loads on **both** emulated servers, pack serving (§2), and the 404 behavior.

## 2. Browser walk against the emulator

```powershell
python tools/hw_emulator.py
# Portal:      http://127.0.0.1:8080/   (or the port the emulator prints)
# Management:  the second server the emulator prints
```

| Step | Browser language list | Open | Expected |
|------|----------------------|------|----------|
| 2.1 | German first (`de`) | portal `/` | Entire page German on first load; view-source shows `<html lang="de">` |
| 2.2 | German first | portal → click *Scan*, then submit bogus credentials | "Scanning…"/status/error messages in German (SC-001 dynamic strings) |
| 2.3 | French (`fr-FR`) | portal `/` | French (regional-variant mapping) |
| 2.4 | Ukrainian (`uk`) | management `/` | All cards incl. Help in Ukrainian; record counts / OTA messages localized (SC-002) |
| 2.5 | Japanese only (`ja`) | both pages | Complete English, zero blank elements (SC-003) |
| 2.6 | German first | both pages | No raw key names (e.g. `mgmt_ota_failed`) visible anywhere |

Layout check while on 2.1/2.4: German/Ukrainian strings (typically longest) must not break the phone-width layout — use devtools mobile viewport.

## 3. Fault injection — broken pack delivery (SC-005, User Story 3)

```powershell
# Temporarily set SERVE_I18N = False in tools/hw_emulator.py (class BaseEmuHandler), restart emulator
```

With browser set to German, open the portal and complete the full setup flow (scan → select → submit → status polling).

**Expected**: page renders entirely in English, every control works, setup completes; no console errors besides the failed pack fetch. Revert the flag afterwards.

Per-string fallback (FR-006): run `python tools/check_i18n.py` after deleting one key from `de.json` locally — checker must fail; in the browser that one element shows English while the rest stays German. Revert.

## 4. On-device validation

```powershell
idf.py build flash monitor
```

1. **Unit tests still pass**: run the component test app for `web_server` (`test_i18n.c` — unchanged vectors).
2. **Portal (AP mode)**: factory-reset the device (hold ← 5 s), join the setup hotspot from a phone set to German → portal fully German; complete real WiFi setup → status messages German (SC-001 on hardware).
3. **Management (STA mode)**: browse to `https://weather-<suffix>.local` with the same phone → management page German; switch phone to an unsupported language, reload → English (SC-003).
4. **Header spot-check**:
   ```powershell
   curl.exe -s -D - -o page.html -H "Accept-Language: uk,en;q=0.5" http://192.168.4.1/
   # Expect: Content-Language: uk   and   <html lang="uk"> in page.html
   ```

## Pass criteria summary

| Success criterion | Proven by |
|-------------------|-----------|
| SC-001 portal 100% localized | §2.1–2.3, §4.2 |
| SC-002 management 100% localized | §2.4, §4.3 |
| SC-003 unsupported → complete English | §2.5, §4.3 |
| SC-004 header vectors E2E | §1 pytest, §4.4 |
| SC-005 broken delivery → usable English | §3 |
| SC-006 inventory consistency, zero discrepancies | §1 checker |
