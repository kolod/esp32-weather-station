# Quickstart: WiFi Details Screen (008)

Validation guide — proves the feature end-to-end. Contracts:
[wifi-details-screen.md](./contracts/wifi-details-screen.md); entities:
[data-model.md](./data-model.md).

This is a display-only feature — there is no emulator path and no host unit test. Validation is
the build gate, the existing regression suites (must stay green), and on-device checks.

## Prerequisites

- ESP-IDF v6.0.2 exported (see `memory/esp-idf-env-setup.md` for this machine's env quirks)
- LilyGO T-Display-class board with both buttons, flashable via `idf.py flash`
- A 2.4 GHz WiFi network the device has (or can be given) credentials for
- Repo root as working directory

## 1. Firmware build gate (Constitution III)

```powershell
idf.py build          # MUST succeed with zero warnings
```

## 2. Non-regression suites (SC-005)

```powershell
python tools/test_hw_emulator.py     # web-server emulator suite — unchanged, must pass
python tools/check_i18n.py           # language-pack parity — unchanged, must pass
```

Expected: identical results to `main` — this feature touches neither surface.

## 3. On-device — connected (STA) mode  (User Story 1)

1. Flash (`idf.py flash`) and let the device join WiFi (centre glyph turns blue).
2. **Long-press the right button** (~1.5 s hold).
   - Within 1 s the weather quadrants are replaced by the WiFi details overlay (SC-002).
   - Three labelled lines: `Network` = your SSID, `IP` = the device's DHCP address,
     `Host` = `weather-XXXX.local` (FR-002, FR-003, FR-004, FR-005).
3. Do nothing for 10 s → overlay disappears, weather view returns (FR-007, SC-003).
4. Long-press right again to show it, then **long-press right once more** → overlay hides
   immediately (FR-008).
5. Type the shown IP (and separately `http://weather-XXXX.local/`) into a browser on the same
   LAN → the device's page loads, proving the shown values are correct (SC-001).
6. **Non-regression while overlay is up** (FR-009):
   - short-click the right button → overlay stays; when it closes, temperature unit has
     toggled °C↔°F.
   - short-click the left button → when overlay closes, time mode has toggled LOCAL↔UTC.
7. **Updates not lost** (FR-010): show the overlay, wait for a sensor sample interval, let it
   close → pressure/temp/humidity reflect the newer reading, not a stale one.

## 4. On-device — AP fallback mode  (User Story 2)

1. Erase credentials (long-press **left** button = factory reset) so the device starts its own
   AP (`weather-XXXX`, centre glyph grey).
2. Long-press the right button.
   - `Network` = `weather-XXXX` (the broadcast SSID, not a joined one).
   - `IP` = `192.168.16.1` (`WIFI_MGR_AP_IP_STR`).
   - `Host` = `weather-XXXX.local`.
   - Matches SC-004: connect a phone to `weather-XXXX`, browse to `192.168.16.1` → portal opens.
3. During the connecting phase (right after entering credentials, before it gets an IP):
   long-press right → `Network` shows the SSID being joined, `IP` shows `---` (FR-006).

## 5. Edge checks

- **Rapid toggling**: long-press right 5–6 times quickly → always ends in a consistent state
  (either overlay shown with a fresh 10 s timer, or hidden), never blank, never stuck.
- **Long SSID**: temporarily join a network whose name is ~30 chars → `Network` value clips
  with `…`, layout does not shift, other lines still readable (FR-011).
- **WiFi drop while shown**: show the overlay on STA, then power off the AP → overlay persists
  until the 10 s timeout (or shows the new state at next redraw); never a mix of AP + STA data.

## Done when

- `idf.py build` clean; both regression suites green (SC-005, SC-006 by inspection — no
  blocking work added to sensor/web tasks).
- Sections 3 and 4 pass on hardware; all FRs observed.
