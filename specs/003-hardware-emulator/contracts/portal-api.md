# Contract: Captive Portal Endpoint (emulator port 8081)

Mirror of the firmware portal HTTP server (`portal_server.c`, `handlers_common.c`).
Emulator serves `http://127.0.0.1:8081`. Deviations marked **[DEV]**.

## Captive-portal probes

`GET /generate_204`, `/gen_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt`
→ `302 Found`, `Location: http://127.0.0.1:<portal-port>/` **[DEV: device redirects to `http://192.168.4.1/`; emulator targets itself so the link resolves locally]**, empty body.

## Static assets (read from `components/web_server/www/portal/` and `www/i18n/` per request)

| Route | Content-Type | Headers / behavior |
|-------|--------------|--------------------|
| `GET /` | `text/html; charset=utf-8` | `Content-Language: <lang>` from `Accept-Language` (en/de/fr/uk, q-values, fallback en); `Cache-Control: no-cache`; `<html lang="…">` attribute patched to `<lang>` **[superseded: the original `data-lang` injection is replaced by the lang-attribute patch, implemented in firmware too — see `specs/004-fix-web-i18n/contracts/i18n-http.md`]** |
| `GET /portal.css` | `text/css` | `Cache-Control: no-cache` **[DEV: device `max-age=3600`; no-cache required for live edit]** |
| `GET /portal.js` | `application/javascript` | same as css |
| `GET /i18n/<lang>.json` | `application/json` | serves `www/i18n/<lang>.json`; only `en|de|fr|uk` accepted, else 404 **[implemented in firmware since spec 004 — see `specs/004-fix-web-i18n/contracts/i18n-http.md`]** |
| `GET /i18n.js` | `application/javascript` | serves `www/common/i18n.js` (shared i18n applier; spec 004) |

## `GET /api/scan` → 200

```json
{ "networks": [ { "ssid": "HomeNet", "rssi": -45, "secure": true }, ... ] }
```

≤ 20 entries; fixed catalog including designated SSIDs `Emu-WrongPass` (forces
auth failure) and `Emu-NotFound` (forces not-found). Small artificial delay
(~1 s) so the page's "Scanning…" state is visible.

## `POST /api/wifi`

Request (flat JSON): `{ "ssid": "...", "password": "...", "tz_name": "..." }`
(`password`, `tz_name` optional).

| Condition | Response |
|-----------|----------|
| Empty body | `400` `{"error":"empty_body"}` |
| Missing/empty/>32-char ssid | `400` `{"error":"invalid_ssid"}` |
| Password > 63 chars | `400` `{"error":"password_too_long"}` |
| Accepted | `202` `{"status":"connecting"}` — starts JoinSession |

Valid `tz_name` is applied to emulator settings; invalid is silently ignored (firmware behavior).

## `GET /api/wifi/status` → 200

One of (exact firmware shapes):

```json
{ "state": "connecting", "ip": null, "reason": null }
{ "state": "connected", "suffix": "A1B2", "reason": null }
{ "state": "failed", "ip": null, "reason": "auth" }
{ "state": "failed", "ip": null, "reason": "not_found" }
{ "state": "failed", "ip": null, "reason": null }
```

Resolution ~2 s after the join POST; outcome from designated SSID or `scenario.join_outcome`.
Before any join attempt: reports `failed` with `reason: null` (firmware initial `JOIN_IDLE` maps to the fallback string) **[note: pages only poll after POSTing, so this state is normally unobserved]**.

## `GET /api/timezones` → 200

Identical to the management contract (same catalog).

## Errors

Unknown route → `404` + console log line.
