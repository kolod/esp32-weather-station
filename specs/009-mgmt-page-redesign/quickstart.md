# Quickstart / Validation: Management Page Redesign & Live Readings

Validates the spec's user stories and success criteria. Assumes the ESP-IDF v6.0.2
environment is active (see project memory `esp-idf-env-setup`) and a BME280 (or the
hardware emulator, spec 003) is attached.

## Prerequisites

- Device provisioned with WiFi + device certificate (`/storage/certs/device.crt`).
- CA cert trusted in the test browser (mgmt page Help → "Install CA Certificate").
- `python` on PATH for the i18n check.

## Build & flash

```powershell
idf.py build              # MUST succeed with zero warnings (Principle III)
idf.py -p <PORT> flash monitor
```

Confirm in the boot log: `mgmt_server: HTTPS management server started on :443`.

## i18n parity gate

```powershell
python tools/check_i18n.py    # MUST exit 0
```

Expected: no "missing key" / "packs differ" errors; zero unused-key warnings after the
obsolete `mgmt_th_*` / `mgmt_btn_load_hist` / `mgmt_btn_bootlog` keys are removed.

## Scenario A — Readings grid layout (US1 / FR-001..005, SC-001, SC-009)

1. Open `https://<device>/` in a desktop browser.
2. **Expect** the Current Readings card as a 2×2 grid: time top-left, temperature
   top-right, pressure bottom-left, humidity bottom-right; time-source line and
   WiFi-status line as full-width rows beneath.
3. Swap the sensor to a BMP280 (or emulate `sensor: bmp280`) and reload → humidity
   quarter shows `---`, other three render.
4. DevTools device toolbar → width 360 px → **expect** no horizontal page scroll; grid
   collapses to one column; all values readable.

## Scenario B — Live updates over WebSocket (US2 / FR-006..011, SC-002)

1. Open the page; DevTools → Network → WS → confirm a `wss://<device>/api/ws`
   connection in state 101/Open.
2. Watch the temperature value; **expect** it to change within ~1 s of each sensor
   sample (~5 s cadence) with **no page reload** and no full-card flicker (only the
   changed numbers update).
3. DevTools Console: `performance.now()` before/after an observed change ≤ 1000 ms for
   ≥ 19 of 20 samples (SC-002).
4. Open the same page in a second tab/device → **expect** both receive updates
   simultaneously (FR-009).

## Scenario C — Reconnect & fallback (US2 / FR-008, SC-003, SC-004)

1. With the page open, disable the device WiFi / block the route for ~20 s.
2. **Expect** the WS to close; DevTools shows periodic reconnect attempts with
   *increasing* gaps (≈1, 2, 4, 8 s…), and `GET /api/status` polling resumes (≤ 5 s
   apart) so values still refresh (SC-004).
3. Restore connectivity → **expect** the WS re-establishes within ~15 s and polling
   stops (SC-003).
4. Force WS off entirely (DevTools request blocking on `/api/ws`) and reload →
   **expect** the page still updates via polling at least every 10 s.

## Scenario D — Capacity cap (FR-012)

1. Open the page in 5+ tabs/devices.
2. **Expect** the first ~4 keep a live WS; extras fall back to polling; the device log
   shows no socket-exhaustion errors and free heap stays > 40 KB
   (`idf.py monitor` → check `esp_get_free_heap_size` log or add a temporary print).

## Scenario E — History plot & period selector (US3 / FR-013..019, SC-005, SC-006)

1. Scroll to the History card → **expect** a line plot (not a table) showing the last
   24 h by default, with **Day** selected in the period selector.
2. Click **Week**, **Month**, **All** in turn → **expect** the plot redraws for each
   range in < 2 s; when the data has pressure/humidity, those series appear alongside
   temperature and stay readable.
3. Select a period with no records (e.g. **Day** right after a flash erase) →
   **expect** a localized "no data" message, not a broken/blank chart.
4. **All** with a large history → **expect** the plot renders without freezing (points
   are down-sampled); the page stays interactive.

## Scenario F — History controls (US4 / FR-020..022, SC-007)

1. In the History card → **expect** the "Download CSV" control at the **bottom** of the
   card, and **no** "Load last 100 records" button anywhere.
2. Click "Download CSV"; diff the file against a pre-redesign export for the same data
   → **expect** byte-identical content and format (SC-007).

## Scenario G — Header alignment (US5 / FR-023..025, SC-009)

1. At desktop width, visually check: the header caption's left edge lines up with the
   left edge of the card text; `#fw-version`'s right edge lines up with the right edge
   of the card text.
2. Narrow to 360 px → **expect** caption and version both still visible, not
   overlapping.

## Scenario H — Boot-log card (US5 / FR-026, FR-027)

1. Open the Boot Log card → **expect** the boot log shown inline and **no** "Download
   boot.log" button.
2. On a device with no boot log → **expect** the localized "not available" message,
   still no button.

## Regression checks

- Configuration card: change timezone → Save → value persists after reload (FR-029).
- Firmware Update card: begin an OTA upload → progress bar behaves as before (FR-029).
- `components/web_server/test` host tests still pass:
  `idf.py -C components/web_server/test build` (i18n applier).
