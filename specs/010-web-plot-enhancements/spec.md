# Feature Specification: Web Interface Plot Enhancements

**Feature Branch**: `010-web-plot-enhancements`

**Created**: 2026-08-30

**Status**: Draft

**Input**: User description: "improve plot on web interface: fullscreen mode; select curve by clicking curve or legend (selected curve glows); cursor tooltip with time + values and a vertical crosshair line; live current-readings plot from WebSocket data with a right-aligned button in the period-sel row; remove right y-axis; left y-axis shows the selected curve's scale; show the whole selected period (00:00-24:00 / Mon-Sun / 1st-last day of month) with previous/next period buttons; mouse-wheel zoom on the time axis around the cursor with y-axis auto-fit on scroll end and a reset button when zoomed."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Read exact values at a point in time (Priority: P1)

A person viewing the history plot moves the pointer across the chart. A vertical
crosshair line follows the pointer, and a tooltip shows the timestamp at that
position together with each curve's value at that moment. Moving off the plot
area hides the crosshair and tooltip.

**Why this priority**: The plot today only shows trends; users cannot tell what
the temperature was at 14:00 yesterday. Point read-out is the single most
requested capability and delivers value on its own without any other change.

**Independent Test**: Load the management page with history data, hover over the
plot, and confirm the crosshair tracks the pointer and the tooltip reports a
plausible time and one value per visible curve; move the pointer off the plot
and confirm both disappear.

**Acceptance Scenarios**:

1. **Given** a plot with at least two curves and history data, **When** the
   pointer is over the plot area, **Then** a vertical line is drawn at the
   pointer's horizontal position and a tooltip lists the time at that position
   and the value of each visible curve, each with its unit.
2. **Given** the pointer is between two sample points, **When** the tooltip
   updates, **Then** it reports the values of the nearest sample to the pointer
   (or an interpolated value) and a time consistent with the crosshair position.
3. **Given** a curve has a gap (no data) at the pointer position, **When** the
   tooltip updates, **Then** that curve is shown with a "no data" placeholder
   rather than a misleading number.
4. **Given** the pointer leaves the plot area, **When** the next pointer event
   fires, **Then** the crosshair and tooltip are removed.
5. **Given** a touch device, **When** the user touches and drags across the
   plot, **Then** the crosshair and tooltip follow the touch point and clear
   when the touch ends.

---

### User Story 2 - Focus on a single curve (Priority: P2)

A person clicks a curve (or its entry in the legend) to select it. The selected
curve is visually emphasised with a glow; the other curves remain visible but
de-emphasised. The left axis switches to the selected curve's scale and unit, so
its shape and magnitude are easy to read. Clicking the selected curve again (or
clicking empty plot space) clears the selection.

**Why this priority**: With three curves of very different magnitudes on one
plot, individual curves are hard to inspect. Selection plus a matching axis
makes each measurement legible without separate charts. Depends on nothing but
the existing plot.

**Why the right axis is removed**: With axis scaling driven by the selected
curve, a second fixed axis is redundant and adds clutter; a single left axis
that always describes the curve in focus is clearer.

**Independent Test**: Load the plot with multiple curves, click one curve and
its legend item, and confirm the selection glow appears, the other curves dim,
the left axis labels change to the selected curve's range and unit, and there is
no right axis.

**Acceptance Scenarios**:

1. **Given** a plot with multiple curves and no selection, **When** the user
   clicks on or near one curve's line, **Then** that curve becomes selected and
   is rendered with a glow effect while the others are de-emphasised.
2. **Given** a plot with a legend, **When** the user clicks a legend entry,
   **Then** the corresponding curve becomes selected with the same visual
   treatment as clicking the line.
3. **Given** a curve is selected, **When** the selection changes or is set,
   **Then** the left y-axis tick labels and axis caption reflect that curve's
   value range and unit.
4. **Given** a curve is selected, **When** the user clicks the selected curve
   again or clicks empty plot area, **Then** the selection is cleared, all
   curves return to normal emphasis, and the left axis returns to the default
   (primary) curve's scale.
5. **Given** the page renders any history plot, **When** it is displayed,
   **Then** no right-hand y-axis or right-hand axis labels are shown.
6. **Given** a curve is selected, **When** the period, live/history mode, or
   zoom changes, **Then** the selection is preserved as long as that curve is
   still present.

---

### User Story 3 - Navigate whole, calendar-aligned periods (Priority: P2)

A person picks Day, Week, or Month. The plot shows the entire current period on
a fixed span - a full day from 00:00 to 24:00, a full week from its first day to
its last day, or a full calendar month from day 1 to the last day - regardless
of how much data exists in it. Previous and Next buttons step the view one whole
period back or forward.

**Why this priority**: Today each period is a rolling window ending "now", so
the same event appears at a different x-position every refresh and periods
cannot be compared. Fixed, aligned spans with step controls make the history
navigable and comparable.

**Independent Test**: Select Week, confirm the x-axis runs from the first to the
last day of the current week even with only a few hours of data; click Previous
and confirm the axis shifts to the prior whole week; click Next to return.

**Acceptance Scenarios**:

1. **Given** the Day period is selected, **When** the plot renders, **Then** the
   x-axis spans 00:00 to 24:00 of the selected day in the device's active
   timezone, and data only partly filling that day still plots at its correct
   time-of-day position.
2. **Given** the Week period is selected, **When** the plot renders, **Then**
   the x-axis spans the first day to the last day of the selected week.
3. **Given** the Month period is selected, **When** the plot renders, **Then**
   the x-axis spans day 1 to the final day of the selected calendar month.
4. **Given** any aligned period is shown, **When** the user clicks Previous or
   Next, **Then** the view moves exactly one whole period earlier or later and
   the axis labels update accordingly.
5. **Given** the view is on the current (most recent) period, **When** the user
   clicks Next, **Then** the control does not advance past the current period
   (it is disabled or ignored).
6. **Given** a stepped-to period contains no records, **When** the plot renders,
   **Then** the empty-state message is shown for that period while Previous/Next
   remain usable.
7. **Given** the "All" period is selected, **When** the plot renders, **Then**
   it spans the first record to the last record and the Previous/Next controls
   are not applicable.

---

### User Story 4 - View the plot fullscreen (Priority: P3)

A person clicks a control to expand the plot to fill the whole screen, hiding
the page header, other cards, and surrounding chrome. Period controls, legend,
tooltip, selection, and zoom all continue to work in this mode. A clear control
(and the Escape key) returns to the normal page.

**Why this priority**: A larger canvas makes dense multi-day data far easier to
read, but it is an enhancement on top of a working plot rather than a
prerequisite.

**Independent Test**: Click the fullscreen control and confirm only the plot and
its controls remain visible filling the viewport; interact with the tooltip and
period buttons; press Escape and confirm the normal page returns unchanged.

**Acceptance Scenarios**:

1. **Given** the management page is shown, **When** the user activates the
   fullscreen control, **Then** the plot and its controls fill the viewport and
   all other page sections are hidden.
2. **Given** the plot is fullscreen, **When** the user resizes the window or
   rotates the device, **Then** the plot re-fits the new viewport size.
3. **Given** the plot is fullscreen, **When** the user activates the exit
   control or presses Escape, **Then** the page returns to its normal layout
   with the same period, selection, and zoom state as before.
4. **Given** the plot is fullscreen, **When** live readings arrive, **Then** the
   plot keeps updating as it does in normal layout.

---

### User Story 5 - Watch a live session plot (Priority: P3)

A person clicks a right-aligned "Live" button in the period row. The plot
switches to showing the readings received over the live connection since the
page was opened, updating as each new reading arrives. Switching back to Day,
Week, Month, or All restores the stored-history view.

**Why this priority**: Gives immediate visual feedback while watching the
station, but it is additive and the stored-history plot already covers the main
use case.

**Independent Test**: Open the page, leave it open while several readings arrive,
click Live, and confirm the plot shows a growing trace of those readings with
correct timestamps; switch to Day and back and confirm history vs. live views
swap correctly.

**Acceptance Scenarios**:

1. **Given** the page has been open and received live readings, **When** the
   user clicks the Live button, **Then** the plot shows those readings plotted
   against their arrival times.
2. **Given** the Live view is active, **When** a new reading arrives, **Then**
   the plot appends it and keeps the newest data visible.
3. **Given** the Live view is active and the page has just loaded with no
   readings yet, **When** the plot renders, **Then** it shows the empty-state
   message until the first reading arrives.
4. **Given** the Live view is active, **When** the user selects a stored period,
   **Then** the plot returns to the stored-history view and the Live button is
   no longer marked active.
5. **Given** the Live button, **When** the period row is displayed, **Then** the
   Live button is right-aligned within that row, visually separated from the
   period buttons.
6. **Given** the live connection drops and reconnects, **When** readings resume,
   **Then** the Live view continues appending without losing the earlier
   session data.

---

### User Story 6 - Zoom the time axis with the wheel (Priority: P3)

A person rotates the mouse wheel over the plot. The time axis zooms in or out,
centred on the time under the pointer. While zooming, the curves stay put; when
wheel activity stops, the y-axis re-fits to the data currently in view. A
"Reset" control appears whenever the view is zoomed and restores the full
period.

**Why this priority**: Powerful for drilling into a spike within a long period,
but the aligned-period navigation already covers most needs and this is the most
complex interaction.

**Independent Test**: On a Week view, point at a feature, scroll to zoom in, and
confirm the time axis narrows around that point; stop scrolling and confirm the
y-axis rescales to the visible data; click Reset and confirm the full week
returns.

**Acceptance Scenarios**:

1. **Given** a plot showing a period, **When** the user scrolls the wheel up
   over the plot, **Then** the visible time range shrinks around the time under
   the pointer.
2. **Given** a zoomed-in plot, **When** the user scrolls the wheel down,
   **Then** the visible time range grows around the time under the pointer, not
   past the full period bounds.
3. **Given** the user is actively scrolling, **When** successive wheel events
   fire, **Then** the y-axis scale is held steady and only the time axis
   changes.
4. **Given** wheel activity has stopped, **When** a short settle interval
   passes, **Then** the y-axis re-fits to the minimum and maximum of the
   currently visible data (of the selected curve if one is selected).
5. **Given** the plot is zoomed to less than the full period, **When** it
   renders, **Then** a Reset control is visible.
6. **Given** a zoomed plot, **When** the user activates Reset, **Then** the view
   returns to the full selected period and the Reset control is hidden.
7. **Given** the wheel is used over the plot, **When** the zoom changes, **Then**
   the page itself does not scroll.

---

### Edge Cases

- **Single data point or empty period**: crosshair/tooltip, selection, and zoom
  degrade gracefully; the empty-state message is shown when there is nothing to
  plot.
- **All curves hidden by selection logic**: at least the selected curve (or the
  primary curve when none is selected) is always drawn.
- **Very long "All" range**: tooltip time formatting stays readable (date vs.
  time granularity adapts to the visible span).
- **Timezone changes** while the page is open: aligned period boundaries and
  tooltip times follow the device's active timezone.
- **DST transition within a Day/Week period**: the day is still presented as
  00:00-24:00 local; a 23- or 25-hour day is acceptable as long as sample
  positions stay correct.
- **Wheel zoom near a period edge**: zoom clamps to the period bounds rather
  than revealing empty space beyond them.
- **Fullscreen exit via browser gesture** (not the app's control): the page
  layout is restored consistently.
- **Rapid period switching**: a slow history response for a superseded period
  does not overwrite the plot for the currently selected one.
- **Selected curve disappears** (e.g. humidity absent in a stepped-to period):
  selection falls back to no selection and the default axis.
- **Live session memory growth**: the number of retained live readings is
  bounded so a page left open for days does not exhaust browser memory.

## Requirements *(mandatory)*

### Functional Requirements

#### Point read-out

- **FR-001**: The plot MUST display a vertical crosshair line at the pointer's
  horizontal position whenever the pointer is over the plot area.
- **FR-002**: The plot MUST display a tooltip showing the time at the crosshair
  position and, for each visible curve, that curve's value at that time with its
  unit.
- **FR-003**: When a curve has no data at the crosshair position, the tooltip
  MUST indicate "no data" for that curve instead of a numeric value.
- **FR-004**: The crosshair and tooltip MUST clear when the pointer leaves the
  plot area or the touch interaction ends.
- **FR-005**: The tooltip MUST remain fully visible within the plot/viewport
  bounds (repositioning near edges as needed).
- **FR-006**: Crosshair/tooltip interaction MUST work with both mouse and touch
  input.

#### Curve selection

- **FR-007**: Users MUST be able to select a curve by clicking on or near its
  line within the plot.
- **FR-008**: Users MUST be able to select a curve by clicking its legend entry.
- **FR-009**: A selected curve MUST be rendered with a distinct glow emphasis,
  and non-selected curves MUST be visibly de-emphasised while remaining visible.
- **FR-010**: Users MUST be able to clear the selection by clicking the selected
  curve again or clicking empty plot area.
- **FR-011**: The legend MUST indicate which curve, if any, is currently
  selected.
- **FR-012**: Selection MUST persist across period changes, live/history
  switches, fullscreen toggles, and zoom changes, as long as the curve is still
  present; it MUST reset if the curve is no longer present.

#### Y-axis behaviour

- **FR-013**: The plot MUST NOT render a right-hand y-axis or right-hand axis
  labels.
- **FR-014**: When a curve is selected, the left y-axis scale, tick labels, and
  caption MUST correspond to that curve's value range and unit.
- **FR-015**: When no curve is selected, the left y-axis MUST correspond to the
  primary curve (temperature) range and unit.
- **FR-016**: All curves MUST continue to be drawn regardless of which curve
  drives the left axis; each non-axis curve is scaled to fit the plot area.

#### Aligned periods and navigation

- **FR-017**: For the Day period, the plot's time axis MUST span 00:00 to 24:00
  of the selected day in the device's active timezone.
- **FR-018**: For the Week period, the time axis MUST span the first through the
  last day of the selected week.
- **FR-019**: For the Month period, the time axis MUST span the first through
  the last day of the selected calendar month.
- **FR-020**: For every aligned period, data that only partly fills the span
  MUST still be positioned at its true time within the span (no stretching to
  fill).
- **FR-021**: The interface MUST provide Previous and Next controls that move
  the view exactly one whole period earlier or later.
- **FR-022**: The Next control MUST NOT allow navigation beyond the current
  (most recent) period.
- **FR-023**: When a navigated period has no data, the empty-state message MUST
  be shown while Previous/Next remain operable.
- **FR-024**: The currently displayed period range MUST be shown to the user
  (e.g. a date or date-range label).
- **FR-025**: For the "All" period, the time axis MUST span from the first to
  the last available record and Previous/Next MUST be inactive.

#### Fullscreen

- **FR-026**: Users MUST be able to expand the plot to fill the viewport,
  hiding all other page sections and chrome.
- **FR-027**: All plot controls (period buttons, Previous/Next, Live, legend,
  tooltip, selection, zoom, Reset) MUST remain available in fullscreen.
- **FR-028**: Users MUST be able to exit fullscreen via an on-screen control and
  via the Escape key, returning to the prior page layout.
- **FR-029**: The plot MUST re-fit when the viewport size or orientation changes
  in fullscreen.
- **FR-030**: Period, selection, and zoom state MUST be unchanged by entering or
  exiting fullscreen.

#### Live session plot

- **FR-031**: The period row MUST contain a right-aligned "Live" control,
  visually separated from the stored-period buttons.
- **FR-032**: Activating Live MUST switch the plot to show readings received
  over the live connection since the page was opened, plotted against their
  arrival times.
- **FR-033**: The Live view MUST append each newly arrived reading and keep the
  newest data visible.
- **FR-034**: Selecting any stored period MUST switch the plot back to the
  stored-history view and clear the Live active state.
- **FR-035**: The Live view MUST show the empty-state message until the first
  reading of the session arrives.
- **FR-036**: Live session data MUST survive a live-connection drop and
  reconnect without losing earlier session readings.
- **FR-037**: The count of retained live readings MUST be bounded so an
  indefinitely open page does not exhaust browser memory; when the bound is
  reached the oldest readings are discarded.

#### Wheel zoom

- **FR-038**: Rotating the wheel over the plot MUST zoom the time axis in or out,
  centred on the time under the pointer.
- **FR-039**: Wheel zoom MUST be clamped to the bounds of the current period (or
  the full data range for "All" / Live).
- **FR-040**: During active wheel scrolling, the y-axis scale MUST be held
  steady while only the time axis changes.
- **FR-041**: After wheel activity stops (a short settle interval), the y-axis
  MUST re-fit to the visible data - of the selected curve if one is selected,
  otherwise the primary curve.
- **FR-042**: A Reset control MUST be visible whenever the view is zoomed to
  less than the full period/range, and MUST restore the full view when
  activated.
- **FR-043**: Wheel zooming over the plot MUST NOT scroll the page.

#### Cross-cutting

- **FR-044**: All new user-facing text (button labels, tooltip labels,
  "no data", period-range label, "Reset", "Live", "Fullscreen") MUST be
  localizable through the existing translation mechanism and provided in all
  currently supported languages.
- **FR-045**: The enhanced plot MUST continue to work when served directly from
  the device with no outbound network access and no third-party scripts or
  styles (consistent with the page's existing content-security constraints).
- **FR-046**: The plot MUST remain usable on small (phone-width) screens: all
  controls reachable, tooltip legible, interactions responsive.

### Key Entities *(include if feature involves data)*

- **Plot view state**: the current mode (stored period vs. live), the selected
  stored period (Day/Week/Month/All), the anchor period being viewed (which
  day/week/month), the selected curve (or none), and the current zoom range on
  the time axis.
- **Curve / series**: one measurement type (temperature, pressure, humidity)
  with its label, unit, colour, value range, selected/de-emphasised state, and
  its samples over time.
- **History sample**: a timestamp plus temperature and optional pressure and
  humidity values, retrieved for the displayed period.
- **Live session buffer**: an in-memory, bounded, time-ordered list of readings
  received over the live connection since the page was opened.
- **Crosshair read-out**: the time at the pointer position and the per-curve
  value (or "no data") at that time.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user can determine the recorded value of any measurement at any
  visible point in time within 5 seconds, without leaving the page or exporting
  data.
- **SC-002**: A user can isolate and read a single measurement's trend (select
  it, matching axis appears) in 2 interactions or fewer.
- **SC-003**: The same calendar day, week, or month can be revisited and shows
  data at identical x-axis positions on every page load (position variance of
  0).
- **SC-004**: A user can move from any period to the equivalent previous or next
  period in exactly 1 interaction.
- **SC-005**: In fullscreen, the plot uses at least 90% of the viewport area for
  the chart and its controls.
- **SC-006**: After opening the page, a user watching live can see a continuous
  trace of every reading received during the session, with no gaps except where
  the device itself sent none.
- **SC-007**: A user can zoom from a full period to a sub-hour window and back to
  the full period using only the wheel and the Reset control.
- **SC-008**: All plot interactions (hover, select, zoom, period step,
  fullscreen toggle) produce a visible response within 100 ms on a mid-range
  phone.
- **SC-009**: A page left open for 24 hours in Live mode does not measurably
  degrade browser responsiveness (bounded memory).
- **SC-010**: Every new piece of user-facing text appears translated in all
  supported languages with no missing-string fallbacks visible.

## Assumptions

- The plot enhancements apply to the management page history chart; the setup
  portal is out of scope.
- "Primary curve" for default axis purposes is temperature, matching the current
  first-series behaviour.
- Aligned period boundaries and all displayed times use the device's active
  timezone (the same one already used elsewhere on the page); weeks start on the
  locale's conventional first day, defaulting to Monday.
- The live session plot is purely client-side: it stores readings already
  delivered for the on-page display and does not add a new device API or
  persist anything on the device.
- "Glow" is a visual emphasis (e.g. brighter stroke plus soft halo); its exact
  styling is a design detail.
- The tooltip reports the value of the nearest sample to the pointer;
  interpolation between samples is acceptable but not required.
- A bound of roughly a few thousand retained live readings is sufficient; older
  readings are dropped once the bound is reached.
- Wheel zoom operates on the time axis only; there is no vertical zoom and no
  click-drag panning in this feature (Previous/Next covers navigation).
- Existing history retrieval, downsampling, and CSV export behaviour are
  unchanged except for the query range needed to cover a full aligned period.
- The current dependency-free, device-served rendering approach is retained; no
  external charting library is introduced.

## Dependencies

- The existing history retrieval interface must accept an arbitrary from/to time
  range so a full aligned (and possibly past) period can be requested.
- The existing live status channel (WebSocket) continues to deliver readings
  used to build the live session buffer.
- The existing translation/localization mechanism and language files.
