# Feature Specification: BME280 Humidity Support & Quadrant Main Screen

**Feature Branch**: `007-bme280-humidity`

**Created**: 2026-08-29

**Status**: Draft

**Input**: User description: "add support for BME280 sensor: autodetect which sensor to use BME280/BMP280 or ds18b20 as fallback; if BME280 detected display humidity; if BME280 detected log humidity. update main screen: time top-left 1/4, temperature top-right, pressure bottom-left, humidity bottom-right, wifi sign screen center"

## Overview

The station already picks its sensor automatically at boot: a pressure-capable sensor if one is present, otherwise the wired temperature probe (feature 005). This feature adds a third, richer option — a sensor that also measures **relative humidity**. When that sensor is fitted, humidity becomes a first-class reading: shown on the built-in screen and the management page, and recorded in the 3‑month history alongside temperature and pressure. Stations fitted with the older pressure-only sensor or the bare probe keep working exactly as before, just without a humidity value.

At the same time the built-in screen is reorganised into a fixed four-quadrant layout — time, temperature, pressure, humidity, one per corner — with the WiFi status indicator moved to the centre of the screen. The layout is always the same four corners regardless of which sensor is fitted; corners with no reading available show a clear placeholder rather than disappearing.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - See humidity at a glance (Priority: P1)

An owner whose station is fitted with a humidity-capable sensor powers the device on. Alongside time, temperature, and pressure, the built-in screen now also shows the current relative humidity. The management page's current readings show the same humidity value, refreshing on the same cadence as the other readings. Temperature and pressure come from the same sensor.

**Why this priority**: This is the requested capability. Without the live humidity reading the new sensor adds nothing over the pressure-only sensor. It also exercises the whole chain: detection, measurement, shared state, display, and web page.

**Independent Test**: Power a device fitted with a humidity-capable sensor. Within the normal boot period the screen shows temperature, pressure, time, and a plausible humidity value (roughly 20–80 %RH indoors); the management page shows the same humidity; breathing near the sensor briefly raises the humidity reading, proving it is live.

**Acceptance Scenarios**:

1. **Given** a device with a humidity-capable sensor attached, **When** it finishes booting, **Then** the screen shows current temperature, pressure, and humidity, each clearly labelled with its unit.
2. **Given** the device is running with a humidity-capable sensor, **When** the owner opens the management page, **Then** the current readings include humidity, refreshing on the same cadence as temperature and pressure.
3. **Given** ambient humidity changes (e.g., verified against a reference hygrometer), **When** the owner watches the readings, **Then** the displayed humidity follows the real value.
4. **Given** the sensor stops responding while the device runs, **When** readings can no longer be obtained, **Then** temperature, pressure, and humidity all show an "unavailable" state rather than stale values, and normal display resumes if readings return.

---

### User Story 2 - Review humidity history (Priority: P2)

The owner opens the management page and reviews recorded humidity alongside temperature and pressure: the history view, the machine-readable records, and the CSV download all include humidity for the period where a humidity-capable sensor was active.

**Why this priority**: Recording turns a live number into weather insight, but it depends on US1 producing readings first.

**Independent Test**: Run a humidity-equipped device long enough to accumulate several history samples, then confirm the history view, the JSON records, and the downloaded CSV each carry a humidity value for the new samples.

**Acceptance Scenarios**:

1. **Given** a humidity-equipped device has been running past several sampling intervals, **When** the owner opens the history view, **Then** recorded entries show humidity together with temperature, pressure, and timestamp.
2. **Given** history is downloaded as CSV, **When** the file is opened, **Then** it contains a humidity column with values for samples taken while the humidity-capable sensor was active.
3. **Given** history records exist from before this feature (temperature-only or temperature+pressure), **When** the owner views or downloads history, **Then** old records remain intact and readable, with humidity clearly absent rather than shown as zero or a fabricated value.
4. **Given** the retention window fills, **When** the oldest entries are dropped, **Then** humidity history follows the same 3-month retention as temperature and pressure.

---

### User Story 3 - Reorganised four-quadrant main screen (Priority: P2)

The owner looks at the built-in screen and sees a consistent layout: the time in the top-left quarter, the temperature in the top-right, the pressure in the bottom-left, the humidity in the bottom-right, and the WiFi status indicator in the centre. The layout stays the same on every station; a corner whose reading is not available on this hardware shows a placeholder in place of a value.

**Why this priority**: The layout change is user-visible on every station regardless of sensor and is explicitly requested, but it is presentation around the core humidity capability rather than the capability itself.

**Independent Test**: View the screen on three stations — one with a humidity-capable sensor, one with a pressure-only sensor, one with only the probe — and confirm the four corners are always in the same positions with the WiFi indicator centred, values filled where available and a placeholder shown otherwise.

**Acceptance Scenarios**:

1. **Given** any station, **When** the screen is on, **Then** time occupies the top-left quarter, temperature the top-right, pressure the bottom-left, humidity the bottom-right, and the WiFi indicator is in the centre.
2. **Given** a station with only the wired probe, **When** the screen is on, **Then** the pressure and humidity corners show a placeholder (e.g., "--- hPa" / "--- %") rather than being removed, and temperature and time display normally.
3. **Given** the WiFi connection state changes, **When** the owner looks at the centre of the screen, **Then** the WiFi indicator reflects the new state (connected vs not) as it did before this feature.
4. **Given** the temperature unit toggle (°C/°F) and the time mode toggle (local/UTC), **When** the owner presses the buttons, **Then** the affected corner updates in place and the other three corners are unaffected.

---

### User Story 4 - The right sensor is picked automatically (Priority: P3)

The same firmware runs on stations with different sensor fittings, with no configuration by the owner. At startup the device detects what is attached and picks the richest available source: a humidity-capable sensor supplies temperature, pressure, and humidity; a pressure-only sensor supplies temperature and pressure; the wired probe supplies temperature only; with none, all readings show unavailable.

**Why this priority**: This protects the existing fleet (pressure-only and probe-only stations must not regress) and makes a hardware upgrade a plug-and-reboot operation — but it is supporting behaviour around US1–US3.

**Independent Test**: Boot the same firmware with each fitting in turn — humidity-capable sensor, pressure-only sensor, probe only, nothing — and verify respectively: temperature+pressure+humidity; temperature+pressure; temperature only (identical to feature 005 behaviour); all unavailable — with no settings changed between boots.

**Acceptance Scenarios**:

1. **Given** a humidity-capable sensor is attached at power-on, **When** the device boots, **Then** it is used for temperature, pressure, and humidity, even if a pressure-only sensor or the probe is also attached.
2. **Given** a pressure-only sensor (no humidity) is attached at power-on, **When** the device boots, **Then** temperature and pressure come from it and humidity is unavailable (screen shows a placeholder, web page omits or marks it unavailable) — exactly the feature-005 behaviour.
3. **Given** only the wired probe is attached at power-on, **When** the device boots, **Then** temperature comes from the probe and both pressure and humidity are unavailable.
4. **Given** the owner swaps a pressure-only sensor for a humidity-capable one, **When** the device is next rebooted, **Then** humidity readings appear without any settings change.

---

### Edge Cases

- Multiple sensors attached → the richest source wins (humidity-capable > pressure-only > probe); lower-priority sensors are ignored while the chosen one works.
- Humidity-capable sensor present at boot but failing intermittently afterwards → all its readings marked unavailable during gaps; the device does not silently fall back to another sensor mid-run (detection happens at startup only; a reboot re-detects).
- Out-of-range or physically implausible humidity (outside 0–100 %RH) → treated as invalid, shown as unavailable, never recorded to history as real data.
- History spanning a hardware upgrade → temperature-only, temperature+pressure, and temperature+pressure+humidity records coexist in the same view, JSON, and CSV without confusing the reader; missing fields read as "not recorded", never as 0.
- Temperature unit toggle (°C/°F) and time mode toggle (local/UTC) → continue to work in the new quadrant layout; humidity and pressure are unaffected by either toggle.
- Localised pages (feature 004) → every new humidity-related label or status appears in all four supported languages, falling back to English per the existing i18n rules.
- Screen legibility → the four quadrant values plus the centred WiFi indicator remain readable at a glance on the built-in display without overlapping.
- Condensing / saturated environment → a sustained 100 %RH reading is displayed and recorded as a valid measurement, not treated as an error.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST support a humidity-capable environmental sensor (BME280) as a source of ambient temperature, atmospheric pressure, AND relative humidity, refreshed on the existing reading cadence (at least every 5 seconds).
- **FR-002**: At startup the system MUST detect attached sensors and select the reading source automatically by priority: humidity-capable sensor (BME280) → temperature, pressure, humidity; else pressure-only sensor (BMP280) → temperature, pressure, humidity unavailable; else wired probe (DS18B20) → temperature only; else → all readings unavailable. No user configuration is involved.
- **FR-003**: When a humidity-capable sensor is active, the built-in screen MUST show the current relative humidity with its unit, and MUST indicate unavailability rather than showing a stale or placeholder value when a reading cannot be obtained.
- **FR-004**: When a humidity-capable sensor is active, the management page's current readings MUST include humidity, refreshing on the same cadence as temperature and pressure, and MUST omit or mark it unavailable otherwise.
- **FR-005**: When a humidity-capable sensor is active, humidity MUST be sampled into the history log on the same schedule and 3-month retention as temperature and pressure, and exposed wherever temperature and pressure history is exposed (history view, machine-readable records, CSV download).
- **FR-006**: History records created without humidity (pre-upgrade data, or pressure-only / probe-only operation) MUST remain valid and clearly distinguish "no humidity recorded" from a measured value of zero.
- **FR-007**: Invalid, missing, or out-of-range readings (humidity outside 0–100 %RH, or any reading the sensor cannot supply) MUST be marked unavailable within 15 seconds, never displayed as current, and never recorded to history as measurements.
- **FR-008**: All existing temperature and pressure behaviour (°C/°F toggle and its persistence, pressure display and history from feature 005, unavailable indication) MUST continue to work unchanged regardless of which sensor is active; pressure-only and probe-only stations MUST behave exactly as they do after feature 005.
- **FR-009**: All new user-visible text introduced for humidity (labels, unit, unavailable states) MUST be localised in the four supported languages per the existing localisation behaviour.
- **FR-010**: The built-in main screen MUST use a fixed four-quadrant layout: time in the top-left quarter, temperature in the top-right, pressure in the bottom-left, humidity in the bottom-right.
- **FR-011**: The WiFi status indicator MUST be positioned at the centre of the main screen and MUST continue to convey the connection state (connected vs not connected) as it did before this feature.
- **FR-012**: The four-quadrant layout MUST be identical on every station regardless of the detected sensor; a quadrant whose reading is not available MUST show a clear placeholder (dashes with the unit/label) rather than being hidden or left blank.
- **FR-013**: The time quadrant MUST continue to show the current time and its mode indicator (local/UTC), and the temperature quadrant its unit indicator (°C/°F), within their respective quarters.
- **FR-014**: Each quadrant value and the centred WiFi indicator MUST remain legible at a glance on the built-in display, with no overlap between quadrants or with the centre indicator.

### Key Entities

- **Humidity reading**: The most recent relative-humidity measurement with a validity flag and timestamp; consumed by the display, the management page, and the history recorder. Expressed as a percentage of relative humidity (%RH), range 0–100.
- **Sensor configuration**: The set of sensors detected at startup and the resulting capability set (temperature always; pressure when a BMP280/BME280 is active; humidity when a BME280 is active). Fixed until the next reboot.
- **History record (extended)**: Timestamped sample now carrying temperature and, when available, pressure and humidity; older records missing one or both remain readable alongside newer ones.
- **Main screen layout**: The fixed arrangement of four corner readings plus a centred WiFi indicator, independent of sensor fitting.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On a station fitted with a humidity-capable sensor, a valid humidity reading appears on the screen and the management page within 10 seconds of power-on (matching the existing temperature and pressure targets).
- **SC-002**: The displayed humidity agrees with a trusted reference hygrometer within ±5 %RH, and follows a real humidity change within 15 seconds.
- **SC-003**: A pressure-only station and a probe-only station running the new firmware show zero behaviour change beyond the screen re-layout: temperature, pressure (where applicable), history, and web pages work exactly as after feature 005, and no humidity artefacts (blank fields, zeros, error text) appear anywhere.
- **SC-004**: After 24 hours of operation on a humidity-equipped station, the history contains a humidity value for at least 95% of the expected samples, and the CSV download reproduces them all.
- **SC-005**: Upgrading a station is plug-and-reboot: swapping in a humidity-capable sensor and power-cycling yields humidity readings with zero configuration steps; swapping back returns the station to its previous behaviour.
- **SC-006**: When the active sensor is disconnected mid-run, all affected readings show unavailable within 15 seconds, and no stale or invalid value is added to history during the outage.
- **SC-007**: On every station, an observer identifies which corner holds time, temperature, pressure, and humidity, and reads the WiFi state from the centre, within 5 seconds of looking at the screen — with the four corners in identical positions across stations.
- **SC-008**: Every new humidity label and status renders correctly in all four supported languages with no layout breakage and English fallback where a translation is missing.

## Assumptions

- The humidity-capable sensor is a Bosch BME280 and the pressure-only sensor a Bosch BMP280; the two are distinguished at detection time by the sensor itself, sharing the same bus and address range as feature 005's BMP280 wiring. No new wiring or Kconfig pin options are required beyond what feature 005 defined.
- Humidity is displayed and recorded as relative humidity in whole or one-decimal percent (%RH); there is no user-selectable humidity unit.
- Sensor detection happens at startup only; hot-plugging a sensor takes effect at the next reboot, consistent with feature 005.
- When a sensor is detected, lower-priority sensors are ignored for as long as the device runs, even if the chosen sensor later fails mid-run (no silent mid-run source switching; a reboot re-detects). This matches feature 005.
- The four-quadrant layout supersedes feature 005's "hide the pressure label when no BMP280 is fitted" behaviour: the pressure and humidity quadrants are always present and show a placeholder when their reading is unavailable.
- The existing history storage format can be extended to carry humidity while keeping older records (temperature-only and temperature+pressure) readable; the 3-month retention target is unchanged.
- New humidity labels reuse the localisation mechanism delivered by feature 004 (en/de/fr/uk with English fallback).
- The hardware emulator (feature 003) will be extended to simulate humidity readings and the BME280 sensor-fitting configuration so the web and screen surfaces can be validated without hardware.
- The built-in display resolution and orientation are unchanged; the quadrant layout is designed to fit the existing ST7789 135×240 landscape screen.
- Button functions (left: time mode, right: temperature unit) are unchanged; no new buttons or gestures are introduced.

## Dependencies

- Builds directly on feature 005 (BMP280 temperature/pressure, boot-time sensor detection, pressure history and web surfaces) and feature 004 (web i18n). Feature 005 behaviour for pressure-only and probe-only stations is treated as the regression baseline.
