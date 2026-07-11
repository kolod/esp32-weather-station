# Quickstart & Validation Guide: ESP32 Weather Station

**Feature**: 001-weather-station-firmware | **Contracts**: [contracts/](./contracts/)

Runnable procedures that prove each user story end-to-end on real hardware.

## Prerequisites

- Tenstar T-Display ESP32 board (16 MB flash), DS18B20 on GPIO27 with 4.7 kΩ pull-up to 3V3
- ESP-IDF v5.4.x installed and exported (`idf.py --version`)
- USB cable + serial port (below: `COM5`; adjust)
- OpenSSL (for `tools/ca/`), a phone (portal test), a computer on the same LAN

## Build & flash

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

Expected on first boot (no credentials): display shows temperature within 10 s, time area shows `--:--`, log shows AP fallback with SSID `weather-XXXX`.

## US1 — Time & temperature display (P1)

1. Power on with probe attached → temperature visible in landscape ≤ 10 s (SC-001).
2. Pinch the probe → displayed value rises within ~10 s (SC-002).
3. Disconnect probe data wire → display shows the unavailable indicator, not a stale value; reconnect → reading resumes.
4. Before WiFi setup, time shows a clear "not set" state; after setup it matches a reference clock (≤ 2 s, SC-008) and updates every minute.
5. Press **left** button → time toggles local↔UTC with mode indicator; reboot → mode persisted.
6. Press **right** button → temperature toggles °C↔°F with unit indicator; reboot → unit persisted.

## US2 — Captive portal onboarding (P2)

1. Fresh device (or factory reset: hold left button 5 s) → `weather-XXXX` open hotspot appears; suffix = last 2 MAC bytes (compare `esp_read_mac` in boot log).
2. Connect a phone → portal auto-opens (Android and iOS both) without typing a URL.
3. Portal language: set phone to German/French/Ukrainian → page renders in that language; any other language → English (FR-022; verify `Content-Language` header with `curl -H "Accept-Language: uk" http://192.168.4.1/`).
4. Pick network from scan list, enter password, select timezone → page reports progress, then success + `https://weather-XXXX.local`; hotspot disappears ≤ 10 s.
5. Negative path: submit a wrong password → portal reports failure and stays usable for retry (device remains in hotspot mode).
6. Reboot → device joins saved network unattended (also US3-1).
7. Timing the happy path start-to-finish must be < 3 min (SC-003).

## US3 — Reconnection & fallback (P3)

1. Power-cycle router → device back online ≤ 60 s after network returns (SC-004); no user action.
2. Keep the network off past the retry window → `weather-XXXX` hotspot reappears **while the display keeps showing temperature**.
3. Restore network, reboot device → normal STA connection resumes.

## US4 — HTTPS with private CA (P4)

```powershell
# one-time CA
./tools/ca/ca-create.ps1 -Org "Home"
# per device (suffix from the hotspot/hostname, e.g. a1b2)
./tools/ca/device-issue.ps1 -Suffix a1b2
./tools/ca/device-provision.ps1 -Port COM5
```

1. Install `ca.crt` into the OS/browser trust store on the client machine.
2. Browse `https://weather-a1b2.local` → page loads, padlock shown, **no certificate warning** (SC-005); temperature and status visible.
3. `curl --cacert ca.crt https://weather-a1b2.local/api/status` → JSON per [http-api.md](./contracts/http-api.md).
4. From a client **without** the CA installed → browser warns (expected; proves TLS is real).

## US5 — OTA update & rollback (P5)

1. Bump `fw_version`, rebuild → `build/esp32-weather-station.bin`.
2. Upload via management page (or `curl --cacert ca.crt --data-binary @build/esp32-weather-station.bin https://weather-a1b2.local/api/ota`) → progress reaches 100 %, device reboots, `/api/status` shows new version. Under 5 min total (SC-006).
3. Invalid image: `curl ... --data-binary @README.md` → `400 invalid_image`, device untouched.
4. Interrupted update: pull power mid-upload → device boots previous firmware normally.
5. Rollback: flash a build whose self-check deliberately fails (test hook) → after reboot the bootloader returns to the previous version automatically.

## US6 — Temperature history (P6)

1. Run device ≥ 2 h with time synced. Management page → history shows 5-min-spaced readings; no gap > 10 min (SC-009 sampling).
2. `curl --cacert ca.crt "https://weather-a1b2.local/api/history?from=<epoch>&to=<epoch>"` → chronological `{timestamp, temperature}` array matching observed values.
3. Download CSV from the page → opens with ISO timestamps.
4. Power-cycle mid-hour → records already flushed (previous hours) survive; at most the current in-RAM hour is lost.
5. Purge: set device clock/test hook to simulate > 3-month-old monthly file → after daily maintenance the file is gone; window never exceeds 3 months + current month ([storage-schema.md](./contracts/storage-schema.md)).

## Long-run check

Leave the device running 7 days (SC-007): display never freezes, web stays reachable, heap (`/api/status` could expose `free_heap` in debug builds) shows no monotonic decline.

## Unit tests

```powershell
# component logic tests (history codec, Accept-Language parser, tz table, MAC suffix)
idf.py -T components/history -T components/web_server -T components/settings build flash monitor
```

All Unity suites must pass before merging.
