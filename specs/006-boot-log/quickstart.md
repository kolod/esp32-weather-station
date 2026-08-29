# Quickstart: Boot Log (006)

Validation guide — proves the feature end-to-end. Contracts: [bootlog-api.md](./contracts/bootlog-api.md); entities: [data-model.md](./data-model.md).

## Prerequisites

- Python 3.11+ (emulator/tests), ESP-IDF v6.0.2 exported (firmware build)
- Repo root as working directory

## 1. Emulator: HTTP + UI contract (no hardware)

```powershell
# Terminal 1 — emulator, log present (default), BMP280 fitted
python tools/hw_emulator.py --no-browser

# Terminal 2 — contract checks
curl -s -D - http://localhost:8080/api/boot.log        # expect: 200, text/plain, ESP-IDF-style lines
curl -s http://localhost:8080/ | Select-String bootlog  # expect: bootlog section markup
```

Expected: 200 body ≤ 4096 bytes, contains a BMP280-found line and display-init lines.

**Unavailable path (FR-007)**:

```powershell
curl -s -X PUT -d '{"bootlog":"missing"}' http://localhost:8080/emu/scenario
curl -s -D - http://localhost:8080/api/boot.log        # expect: 404 {"error":"not_found"}
```

Open `http://localhost:8080/` in a browser: Boot Log card shows the localized
"not available" message; switch scenario back to `present`, reload — log text and a
working `boot.log` download link appear (US1, US2).

## 2. Automated suites

```powershell
python tools/test_hw_emulator.py     # includes new /api/boot.log + UI section tests
python tools/check_i18n.py           # 3 new mgmt_bootlog_* keys, parity across en/de/fr/uk (SC-005)
```

Expected: all pass, zero regressions (SC-004).

## 3. Host unit tests (buffer logic)

```powershell
# Unity component test app (same harness as components/sensor/test, components/history/test)
# Covers: append, 4 KB cap + truncation marker, tag allowlist filter, ANSI strip
idf.py -C components/boot_log/test build   # or the repo's established test-app invocation
```

## 4. Firmware build gate

```powershell
idf.py build                         # must succeed; SC-004
```

## 5. On-device validation (US1–US3)

1. Flash and power-cycle the device (`idf.py flash`), **without** a serial monitor attached.
2. Wait for it to join WiFi, open `https://<device>/`.
3. **US1**: Boot Log card shows this boot's lines — sensor detection outcome (e.g.
   `BMP280 found at 0x76` or `BMP280 not found at 0x76 or 0x77`) and display-init
   lines — readable in < 60 s from power-on (SC-001).
4. **US2**: click the download link → `boot.log` file saves; contents match the view.
5. **US3**: the lines shown carry early-boot millis timestamps (`(1…2 s)`), proving
   they predate network-up; the page loaded at normal speed (SC-002).
6. Reboot again → card shows only the newest boot's lines (FR-003).
7. Negative check (FR-008): erase/corrupt the storage partition
   (`idf.py erase-flash` then reflash, or temporarily unmount) — device still boots
   and serves the page; Boot Log card degrades to the "not available" message or
   RAM-backed content, never an error page.
