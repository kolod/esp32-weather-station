# Quickstart: Web Interface Plot Enhancements — validation guide

Front-end-only feature. No firmware behaviour changes; a clean build only confirms
the embedded assets still link.

## Prerequisites

- A running target: a flashed device on the LAN, **or** the hardware emulator
  (spec 003).
- The device has some stored history (let it run, or seed via the emulator) — at
  least a few hours across two calendar days makes the aligned-period tests
  meaningful.
- CA certificate trusted so `https://<host>/mgmt` loads without warnings
  (see the page's own Help section).
- Browsers: one desktop (mouse wheel) + one phone (touch). Test at 360 px width.

## Build / deploy

```powershell
# from repo root, ESP-IDF env active (see memory: esp-idf-env-setup)
idf.py build              # must be clean, zero warnings (Principle III)
idf.py flash              # or copy assets into the emulator's serving path
python tools/check_i18n.py   # translation-pack parity after i18n edits
```

Then open `https://<host>/mgmt` (or the emulator URL).

## Scenario 1 — Crosshair + tooltip (US1 / P1)

1. Load `/mgmt`, wait for the history plot to render (Day).
2. Move the mouse across the plot.
   - **Expect**: a vertical line snaps between sample points; a tooltip shows a
     time and one row per visible curve (`Temperature 21.4 °C`, etc.).
3. Hover a region where pressure/humidity have a gap.
   - **Expect**: that row reads the localized "no data", not a number.
4. Move the pointer off the plot.
   - **Expect**: line and tooltip disappear.
5. On the phone: touch-drag across the plot.
   - **Expect**: line + tooltip follow the finger, clear on lift.
6. Hover near the left and right edges.
   - **Expect**: the tooltip box stays fully visible (flips side near the edge).

## Scenario 2 — Curve selection & single axis (US2 / P2)

1. With ≥2 curves shown, click directly on the pressure line.
   - **Expect**: pressure gets a glow; temperature + humidity dim; the **left**
     axis numbers and caption switch to hPa and pressure's range.
2. Confirm there is **no right-hand axis** anywhere.
3. Click the "Temperature" legend entry.
   - **Expect**: selection moves to temperature; axis switches to °C.
4. Click empty plot area (or the selected line again).
   - **Expect**: selection clears; all curves back to normal; axis back to °C
     (temperature default).
5. Select humidity, then switch period Day→Week.
   - **Expect**: humidity stays selected (still present).
6. Step to a past period with no humidity data.
   - **Expect**: selection falls back to none; axis = temperature.

## Scenario 3 — Aligned periods + navigation (US3 / P2)

1. Click **Week**.
   - **Expect**: x-axis runs Monday→Sunday of this week; data only partly fills it;
     a range label shows the week's dates; **Next** is disabled.
2. Click **Prev**.
   - **Expect**: axis shifts to the whole previous week; label updates; **Next**
     now enabled.
3. Click **Next** back to the current week.
4. Click **Day**; verify axis is 00:00–24:00 of today, afternoon empty if it's
   morning.
5. **Prev** several times into a day with no records.
   - **Expect**: empty-state message; **Prev/Next** still work.
6. Click **Month**; verify axis is day 1 → last day of the month.
7. Set the device timezone (Configuration card) to something far from UTC, reload.
   - **Expect**: Day boundaries follow the new local midnight.
8. Click **All**.
   - **Expect**: axis spans first→last record; **Prev/Next** inactive.

## Scenario 4 — Fullscreen (US4 / P3)

1. Click the fullscreen control on the history card.
   - **Expect**: only the plot + its controls fill the viewport; header and all
     other cards hidden.
2. Hover for a tooltip; click a curve; step a period — all still work.
3. Rotate the phone / resize the desktop window.
   - **Expect**: plot re-fits the new size.
4. Press **Escape** (and try the on-screen exit control).
   - **Expect**: normal page returns; same period, selection, and zoom as before.

## Scenario 5 — Live session plot (US5 / P3)

1. Load `/mgmt` and leave it open for several sensor samples (≥ 1–2 min).
2. Click the **Live** button (right-aligned in the period row, visually separated).
   - **Expect**: plot shows a growing trace of the readings received since load,
     timestamps correct.
3. Watch a new reading arrive.
   - **Expect**: the trace extends, newest point visible.
4. Immediately after a fresh reload, click **Live** before any reading.
   - **Expect**: empty-state until the first reading, then it starts.
5. Briefly drop WiFi to the device, restore it.
   - **Expect**: a gap in the Live trace during the outage, then it resumes with
     earlier session data intact.
6. Click **Day**.
   - **Expect**: back to stored history; **Live** no longer marked active.

## Scenario 6 — Wheel zoom (US6 / P3)

1. On **Week**, point at a feature (a bump) and scroll the wheel up.
   - **Expect**: time axis narrows around that point; page does **not** scroll.
2. Keep scrolling in, then stop.
   - **Expect**: during scrolling the y-axis is steady; ~a moment after stopping,
     the y-axis rescales to the visible data.
3. A **Reset** control is visible while zoomed.
4. Scroll down (out) past the full week.
   - **Expect**: clamps at the full period, no empty margin beyond it.
5. Click **Reset**.
   - **Expect**: full week restored; Reset hides.
6. Select a curve, zoom in.
   - **Expect**: the settle-refit uses the selected curve's visible min/max.

## Cross-cutting checks

- **i18n**: switch browser language to de / fr / uk (Accept-Language). Every new
  label — Live, Fullscreen/Exit, Prev, Next, Reset, "no data", range label — is
  translated, no raw keys or English fallbacks visible. `tools/check_i18n.py`
  passes.
- **CSP**: DevTools console shows no CSP violations; no network requests to any
  third-party origin.
- **360 px width**: no horizontal page scroll; all plot controls reachable;
  tooltip legible.
- **Perf**: on the phone, every interaction (hover, select, zoom, period step,
  fullscreen toggle) responds within ~100 ms.
- **24 h soak**: leave the page in Live mode overnight; next morning it is still
  responsive (Live buffer capped at 10 000 entries).
- **Build**: `idf.py build` clean, zero new warnings.

## Reference

- Controller API: [contracts/chart-controller.md](./contracts/chart-controller.md)
- History query windows: [contracts/history-api.md](./contracts/history-api.md)
- View-state model: [data-model.md](./data-model.md)
- Decisions & rationale: [research.md](./research.md)
