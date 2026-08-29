# Quickstart: Local Hardware Emulator

**Feature**: 003-hardware-emulator

## Prerequisites

- Python 3.9+ on PATH (no packages to install — stdlib only)
- Repository checked out (emulator serves pages from `components/web_server/www/`)

## Start

```powershell
python tools/hw_emulator.py
```

Expected within a few seconds (SC-001):

- Console prints the management URL (`http://127.0.0.1:8080/`), portal URL (`http://127.0.0.1:8081/`), and control URL (`http://127.0.0.1:8080/emu`)
- Both pages open automatically in the default browser (FR-014)
- Console logs each served request

Useful variants:

```powershell
python tools/hw_emulator.py --no-browser --mgmt-port 9000 --portal-port 9001
python tools/hw_emulator.py --join-outcome auth --sensor-invalid
```

## Validate — User Story 1 (management page)

1. Management page shows temperature, time/sync info, WiFi block, firmware version, uptime, history/storage figures (all simulated; see [contracts/mgmt-api.md](contracts/mgmt-api.md)).
2. Change timezone / time mode / temperature unit, save → page reflects new values; re-fetch status shows them persisted for the session.
3. Enter an unknown timezone via the API to confirm error path:
   `curl -X PUT http://127.0.0.1:8080/api/config -d '{"tz_name":"Not/AZone"}'` → 400 `unknown_timezone`.
4. History chart renders ~24 h of data; CSV download produces `history.csv` with ISO-8601 rows.
5. **Live edit (SC-002)**: change visible text in `components/web_server/www/mgmt/mgmt.html`, refresh browser → change appears; revert.

## Validate — User Story 2 (portal page)

1. Portal page at `http://127.0.0.1:8081/` — click Scan → network list appears after a brief "Scanning…" state.
2. Join `HomeNet` with any password → status progresses connecting → connected with device suffix.
3. Join `Emu-WrongPass` → failure with the wrong-password message.
4. Join `Emu-NotFound` → failure with the network-not-found message.
5. Captive probe: `curl -i http://127.0.0.1:8081/generate_204` → `302` to the portal root.
6. Language (SC-005): switch browser preferred language to de/fr/uk, reload portal → translated strings (served from `www/i18n/`).

## Validate — User Story 3 (OTA & edge states)

1. On the management page, upload any file (e.g., a few-hundred-KB dummy) as a firmware update → progress advances → success; after ~3 s the emulator simulates reboot (uptime resets, version bump on next status).
2. Open `http://127.0.0.1:8080/emu`, set OTA outcome to `invalid_image`, upload again → page shows the failure message.
3. Toggle `sensor_valid` off → management page shows the invalid-reading presentation; toggle `time_synced` off → page shows not-synced state. (Also scriptable: `curl -X PUT http://127.0.0.1:8080/emu/scenario -d '{"sensor_valid":false}'`.)

## Validate — edge cases

- Start a second instance → clear "port in use" error naming the flag to change it.
- Run from another directory (`cd \; python <repo>\tools\hw_emulator.py`) → still finds `www/` (path resolved relative to script).
- Request an unknown route → 404 and a `MISS` line in the console.

## Unit tests

```powershell
python -m unittest tools.test_hw_emulator -v
```

Covers: Accept-Language picker (cases mirrored from `components/web_server/test/test_i18n.c`), join/OTA state machines, history generation/filtering, config validation.

## Success criteria mapping

| Criterion | How verified here |
|-----------|-------------------|
| SC-001 (<30 s to interactive) | "Start" section — one command, auto-open |
| SC-002 (<10 s edit loop) | US1 step 5 |
| SC-003 (100 % page calls answered) | Browser devtools console/network shows no failed requests during US1/US2 walkthroughs |
| SC-004 (outcomes on demand <1 min) | US2 steps 2–4, US3 steps 1–2 |
| SC-005 (all languages, no restart) | US2 step 6 |
