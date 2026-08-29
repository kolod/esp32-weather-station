# Feature Specification: Management Page Redesign & Live Readings

**Feature Branch**: `009-mgmt-page-redesign`

**Created**: 2026-08-29

**Status**: Draft

**Input**: User description: "update mgmt page: readings card as 2x2 quarters (time top-left, temperature top-right, pressure bottom-left, humidity bottom-right; time-source and wifi-status on bottom); history card shows a plot with a period selector (day-default, week, month, all); move CSV download to the bottom of the history card and remove the load-history button; align the header caption to the left edge of the card content and the firmware version to the right edge; remove the boot-log download button because the boot log is short; use a websocket to update readings faster."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Read current conditions at a glance (Priority: P1)

A user opens the management page on a phone or laptop and immediately sees the four
current measurements — time, temperature, pressure, and humidity — arranged in a
balanced 2×2 grid, with the time source and WiFi status shown beneath them.

**Why this priority**: The current readings are the primary reason a user visits the
page. A clear, scannable layout is the core value of the redesign and can ship on its
own.

**Independent Test**: Load the page with a BME280 attached and confirm the readings
card shows time in the top-left quarter, temperature top-right, pressure bottom-left,
humidity bottom-right, and time-source + WiFi status as full-width rows at the bottom.

**Acceptance Scenarios**:

1. **Given** a BME280 sensor is connected and time is synchronized, **When** the user
   opens the management page, **Then** the readings card displays four quarters —
   time (top-left), temperature (top-right), pressure (bottom-left), humidity
   (bottom-right) — with time-source and WiFi-status rows below the grid.
2. **Given** a BMP280 sensor (no humidity) is connected, **When** the page loads,
   **Then** the humidity quarter shows a placeholder ("---") or is visually empty
   while the other three quarters and the bottom rows render normally.
3. **Given** the sensor has not produced a valid reading yet, **When** the page loads,
   **Then** each affected quarter shows the "---" placeholder rather than a stale or
   blank value.
4. **Given** the page is viewed on a narrow screen, **When** the readings card is
   rendered, **Then** the four quarters remain readable (the grid may collapse to a
   single column) and no content overflows the card.

---

### User Story 2 - Live-updating readings (Priority: P1)

While the management page is open, the displayed readings update within roughly one
second of the device measuring a new value, without the user reloading the page.

**Why this priority**: "Faster updates" is an explicit goal. A persistent live
connection makes the page feel responsive and removes the up-to-5-second lag of the
current poll.

**Independent Test**: Open the page, change the sensor input (or wait for the next
sample), and confirm the on-screen values change within ~1 s without a reload and
without a visible full-card flicker.

**Acceptance Scenarios**:

1. **Given** the management page is open, **When** the device completes a new sensor
   measurement, **Then** the affected readings update on screen within 1 second.
2. **Given** the live connection drops (WiFi blip, device reboot), **When**
   connectivity returns, **Then** the page re-establishes the live connection
   automatically and resumes updating, showing the latest values.
3. **Given** the live connection cannot be established at all, **When** the page is
   open, **Then** the page falls back to periodic refresh so readings still update
   (at a slower cadence) and the user is not left with a frozen page.
4. **Given** multiple clients have the page open at once, **When** a new measurement
   occurs, **Then** all connected clients receive the update.

---

### User Story 3 - Visualize history as a plot with period selection (Priority: P2)

A user wants to see how temperature (and pressure/humidity where available) has
changed over time. The history card shows a line plot with a period selector offering
Day (default), Week, Month, and All. Changing the period redraws the plot for that
range.

**Why this priority**: The history table is hard to read for trends. A plot is a
large usability improvement but is independent of the readings redesign.

**Independent Test**: Open the page with recorded history, confirm the plot loads
showing the last day by default, then switch to Week / Month / All and confirm the
plot redraws with the corresponding data range each time.

**Acceptance Scenarios**:

1. **Given** the page loads, **When** the history card renders, **Then** it shows a
   plot of the last 24 hours (Day) by default, with the Day option selected.
2. **Given** the history card is visible, **When** the user selects Week, Month, or
   All, **Then** the plot redraws to cover that period using the stored history data.
3. **Given** a selected period contains no records, **When** the plot renders, **Then**
   an empty-state message is shown instead of a blank or broken chart.
4. **Given** the stored history includes pressure and/or humidity, **When** the plot
   renders, **Then** those series are shown alongside temperature in a way that keeps
   each series readable.
5. **Given** the "All" period covers a large number of records, **When** the plot
   renders, **Then** it remains responsive (data may be down-sampled for display) and
   the page does not freeze.

---

### User Story 4 - Export and trimmed history controls (Priority: P3)

The history card's "Download CSV" action sits at the bottom of the card, and the
separate "Load last 100 records" button is gone (the plot replaces the manual table
load).

**Why this priority**: Small cleanup that follows from the plot redesign; low risk,
low effort.

**Acceptance Scenarios**:

1. **Given** the history card is rendered, **When** the user looks for the CSV export,
   **Then** it appears as a control at the bottom of the card.
2. **Given** the redesigned history card, **When** the user inspects it, **Then** there
   is no "Load last 100 records" button.
3. **Given** the user activates "Download CSV", **When** the export runs, **Then** it
   downloads the full recorded history as before (unchanged behavior).

---

### User Story 5 - Cleaner header and boot-log card (Priority: P3)

The header caption aligns with the left edge of the card content below it, and the
firmware version aligns with the right edge of that content. The boot-log card shows
its contents inline with no download button.

**Why this priority**: Visual polish and de-cluttering; independent and low risk.

**Acceptance Scenarios**:

1. **Given** the page is rendered at a typical desktop width, **When** the user
   compares the header to the cards, **Then** the caption's left edge lines up with
   the left edge of the card text and the firmware version's right edge lines up with
   the right edge of the card text.
2. **Given** the boot-log card is rendered and a boot log exists, **When** the user
   views it, **Then** the boot-log text is shown inline and there is no "Download
   boot.log" button.
3. **Given** no boot log is available, **When** the boot-log card renders, **Then** it
   shows the localized "not available" message and still has no download button.

---

### Edge Cases

- Live connection is repeatedly interrupted: reconnection attempts MUST be rate-limited
  (backoff) so a flapping network does not spam connection attempts.
- Device time is not synchronized: the time quarter shows "--:--" with a "no sync"
  indicator; the plot still renders using record timestamps.
- Very large history ("All" with months of hourly records): display data is
  down-sampled so render stays responsive; CSV export is unaffected.
- Sensor type changes between page loads (e.g., BMP280 replaced by BME280): on next
  load the humidity quarter and humidity series appear without further user action.
- Narrow / mobile viewport: 2×2 grid collapses gracefully; plot remains legible and
  horizontally contained within its card.
- Language switch: all new labels (period selector options, plot axis/legend text,
  quarter labels) are covered by the existing localization mechanism for en/de/fr/uk.

## Requirements *(mandatory)*

### Functional Requirements

#### Readings card layout

- **FR-001**: The readings card MUST arrange its four primary values in a 2×2 grid:
  time in the top-left quarter, temperature in the top-right, pressure in the
  bottom-left, humidity in the bottom-right.
- **FR-002**: The time-source line and the WiFi-status line MUST remain below the 2×2
  grid as full-width rows.
- **FR-003**: Each quarter MUST show a clear label and, when its value is unavailable
  or invalid, a placeholder ("---" / "--:--") rather than a blank or stale value.
- **FR-004**: When the connected sensor does not provide a measurement (e.g., humidity
  on a BMP280), that quarter MUST show the placeholder; the remaining quarters MUST
  render normally.
- **FR-005**: The readings card MUST remain readable on narrow viewports, collapsing
  the grid as needed without content overflow.

#### Live readings

- **FR-006**: While the management page is open, current readings (temperature,
  pressure, humidity, time, time-source, WiFi status) MUST update automatically within
  1 second of a new device measurement, without a page reload.
- **FR-007**: The page MUST use a persistent push connection (WebSocket) for readings
  updates as the primary mechanism.
- **FR-008**: If the push connection cannot be established or is lost, the page MUST
  fall back to periodic refresh so readings continue to update, and MUST attempt to
  restore the push connection with a rate-limited (backoff) retry.
- **FR-009**: The device MUST broadcast readings updates to all connected management
  clients.
- **FR-010**: Readings updates MUST refresh only the changed values (no full-card
  re-render / flicker).
- **FR-011**: The push connection MUST be served over the same secured management
  channel as the rest of the management interface (no plaintext downgrade).
- **FR-012**: The number of simultaneous push connections MUST be bounded; additional
  connections beyond the limit are refused without destabilizing existing clients.

#### History plot

- **FR-013**: The history card MUST present recorded data as a time-series plot instead
  of the always-visible table.
- **FR-014**: The history card MUST provide a period selector with options Day, Week,
  Month, and All; Day MUST be the default on page load.
- **FR-015**: Selecting a period MUST redraw the plot to cover that range using stored
  history data.
- **FR-016**: The plot MUST show temperature, and MUST additionally show pressure and
  humidity series when the stored data for the selected period contains them.
- **FR-017**: When the selected period has no records, the card MUST show an empty-state
  message instead of a broken or blank chart.
- **FR-018**: For large periods, the plot MAY down-sample data for rendering so the
  page stays responsive; the underlying stored data and CSV export MUST NOT be altered.
- **FR-019**: The plot MUST remain horizontally contained within its card on all
  supported viewport widths.

#### History controls

- **FR-020**: The "Download CSV" control MUST be positioned at the bottom of the
  history card.
- **FR-021**: CSV export MUST continue to download the complete recorded history with
  unchanged content and format.
- **FR-022**: The "Load last 100 records" button MUST be removed from the history card.

#### Header alignment

- **FR-023**: The header caption's left edge MUST align with the left edge of the card
  content area.
- **FR-024**: The firmware-version element's right edge MUST align with the right edge
  of the card content area.
- **FR-025**: Header alignment MUST degrade gracefully on narrow viewports (caption and
  version remain visible and non-overlapping).

#### Boot-log card

- **FR-026**: The "Download boot.log" button MUST be removed from the boot-log card.
- **FR-027**: The boot-log card MUST continue to display the boot-log contents inline
  when available, and the localized "not available" message when not.

#### Localization & consistency

- **FR-028**: All new user-visible text (quarter labels, period-selector options,
  plot legend/axis labels, empty-state and connection-status messages) MUST be
  provided through the existing localization mechanism for en, de, fr, and uk.
- **FR-029**: The redesign MUST NOT change the behavior of the Configuration or
  Firmware Update cards.

### Key Entities *(include if data involved)*

- **Readings update**: A snapshot of current device state pushed to the page —
  temperature (°C), pressure (hPa, optional), humidity (%, optional), current time and
  time mode, time source, WiFi state/SSID/IP, firmware version, history record count.
- **History series**: An ordered set of timestamped records over a selected period,
  each carrying temperature and optionally pressure and humidity, used to draw the
  plot.
- **Period selection**: One of {Day, Week, Month, All} determining the time window of
  the history series shown; Day is the default.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On page load, a first-time viewer can identify the current time,
  temperature, pressure, and humidity within 5 seconds, each in its designated
  quarter.
- **SC-002**: With the page open, a new device measurement is reflected on screen in
  under 1 second in at least 95% of updates on a healthy local network (versus up to
  5 seconds today).
- **SC-003**: After a simulated network drop, the page resumes live updating within 15
  seconds of connectivity returning, with no user action.
- **SC-004**: If the live connection is unavailable, readings still update at least
  once every 10 seconds via fallback.
- **SC-005**: A user can view temperature trends for the last day, week, month, and
  full history by making a single selection each, with the plot redrawing in under 2
  seconds for each period on a typical dataset.
- **SC-006**: The "All" period renders without the page becoming unresponsive even
  with the maximum stored history (device flash capacity).
- **SC-007**: CSV export output is byte-for-byte equivalent to the pre-redesign export
  for the same stored data.
- **SC-008**: All new labels and messages display correctly in en, de, fr, and uk with
  no missing-translation placeholders.
- **SC-009**: At 360 px viewport width, no card produces horizontal page scrolling and
  all readings/plot content stays within its card.

## Assumptions

- The redesign targets the management page (`/mgmt`) only; the captive portal and the
  on-device TFT UI are out of scope.
- Supported languages remain en, de, fr, uk (existing set).
- The existing history storage, `/api/history`, `/api/status`, `/api/history.csv`, and
  `/api/boot.log` endpoints remain available; the WebSocket is added alongside them and
  `/api/status` continues to work as the fallback source.
- "Faster" is defined as sub-second update latency; the device's sensor sampling
  interval (≥ 5 s per constitution) is unchanged — pushes simply remove client poll
  lag and deliver values as soon as they exist.
- A lightweight client-side charting approach is acceptable; no external CDN
  dependency is introduced (embedded resource discipline) — the plot is drawn with
  assets served from the device.
- The maximum number of concurrent management WebSocket clients is small (assume ≤ 4)
  given the home-network single-device context.
- Down-sampling for the "All" view is display-only and does not need to be
  configurable by the user.
- Header "card content" edges refer to the inner text/content box of the cards in the
  main column (excluding card padding is acceptable as long as it reads as aligned).
