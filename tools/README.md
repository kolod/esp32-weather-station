# Hardware emulator for web UI development

`hw_emulator.py` runs two local HTTP endpoints that emulate the weather
station's web servers, so the management page and the captive portal can be
developed and tested in a normal browser — no device, no firmware build, no
flashing. It serves the real page sources from `components/web_server/www/`
and re-reads them on every request: edit a file, refresh the browser, see the
change. The pages themselves are never modified and stay fully compatible
with the real ESP32 firmware.

Requires only a stock Python 3.9+ — no packages, no frameworks.

## Usage

```sh
python tools/hw_emulator.py
```

Prints the URLs and opens both pages in the default browser:

- Management page: `http://127.0.0.1:8080/`
- Captive portal: `http://127.0.0.1:8081/`
- Scenario control: `http://127.0.0.1:8080/emu` (switch failure/edge states at runtime)

Common variants:

```sh
python tools/hw_emulator.py --no-browser --mgmt-port 9000 --portal-port 9001
python tools/hw_emulator.py --join-outcome auth --sensor-invalid   # start in failure states
python tools/hw_emulator.py --help                                 # all flags
```

In the portal, joining `Emu-WrongPass` / `Emu-NotFound` always produces the
wrong-password / network-not-found failure, regardless of scenario settings.

### Boot log (spec 006)

The management endpoint serves `GET /api/boot.log` — a synthetic ESP-IDF-style
startup log whose sensor-detection lines follow the active `sensor` scenario
(`bmp280` → found; `ds18b20` → BMP280-not-found + DS18B20-found; `none` → both
not found), plus display-init lines. The scenario field `bootlog` controls
availability: `"present"` (default) returns `200 text/plain`, `"missing"`
returns the `404 {"error": "not_found"}` shape so the management page's
"not available" state can be exercised. Switch it at runtime via `/emu` or
`PUT /emu/scenario`, or start with `--bootlog missing`. Contract:
`specs/006-boot-log/contracts/bootlog-api.md`.

Unit tests: `python -m unittest tools.test_hw_emulator -v`

Full validation walkthrough and endpoint contracts:
`specs/003-hardware-emulator/` (quickstart.md, contracts/).

## Fidelity notes

- Binds to localhost only; plain HTTP (the device's management server is
  HTTPS — pages use relative URLs, so behavior is identical).
- All responses send `Cache-Control: no-cache` so edits always show on
  refresh (the device caches css/js for an hour).
- i18n matches the firmware (spec 004): both endpoints patch the page's
  `<html lang="…">` attribute from `Accept-Language`, send a matching
  `Content-Language` header, and serve `/i18n/<lang>.json` plus the shared
  applier `/i18n.js`. Contract:
  `specs/004-fix-web-i18n/contracts/i18n-http.md`. Setting
  `BaseEmuHandler.SERVE_I18N = False` disables pack/applier delivery to
  validate the English-fallback path (quickstart §3).
- i18n consistency check (packs ↔ pages ↔ scripts):
  `python tools/check_i18n.py` (also runs as part of the unit tests).
