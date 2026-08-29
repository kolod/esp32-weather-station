# Feature Specification: BMP280 Temperature/Pressure Sensor

**Feature Branch**: `005-bmp280-sensor`

**Created**: 2026-07-14

**Status**: Draft

**Input**: User description: "add bmp280 sensor for themperature/presure"

## Overview

The weather station currently measures temperature only, through a wired probe. This feature adds support for a BMP280 sensor, which measures both temperature and atmospheric pressure. The device detects at startup which sensor is attached: when a BMP280 is present it becomes the source of both readings; when only the wired probe is present the device behaves exactly as it does today (temperature only). Pressure becomes a first-class reading — shown on the built-in screen and the management page, and recorded in the 3-month history alongside temperature.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - See atmospheric pressure at a glance (Priority: P1)

An owner whose station is fitted with a BMP280 sensor powers the device on. Alongside the time and temperature it already shows, the built-in screen now also shows the current atmospheric pressure. The management web page's current readings show the same pressure value, refreshing automatically. Temperature on both surfaces comes from the BMP280.

**Why this priority**: This is the requested capability itself — without the live pressure reading the new sensor adds nothing. It also exercises the whole chain: measurement, shared state, display, and web page.

**Independent Test**: Power a device fitted with a BMP280. Within the normal boot period the screen shows temperature, time, and a plausible pressure value (roughly 950–1050 hPa at typical altitudes); the management page shows the same pressure; gently warming the sensor moves the temperature reading, proving the BMP280 (not the probe) is the source.

**Acceptance Scenarios**:

1. **Given** a device with a BMP280 attached, **When** it finishes booting, **Then** the screen shows current temperature and pressure, and the pressure value is clearly labeled with its unit.
2. **Given** the device is running with a BMP280, **When** the owner opens the management page, **Then** the current readings include pressure, and it refreshes on the same cadence as temperature.
3. **Given** ambient pressure changes (e.g., verified against a local reference), **When** the owner watches the readings, **Then** the displayed pressure follows the real value.
4. **Given** the BMP280 stops responding while the device runs, **When** readings can no longer be obtained, **Then** both temperature and pressure show an "unavailable" state rather than stale values, and normal display resumes if readings return.

---

### User Story 2 - Review pressure history (Priority: P2)

The owner opens the management page and reviews recorded pressure alongside temperature: the history table, the machine-readable records, and the CSV download all include pressure for the period where a BMP280 was active. Pressure trends (rising/falling) are the main forecasting value of a barometer, so history matters more for pressure than for temperature.

**Why this priority**: Recording is what turns a live number into weather insight, but it depends on US1 producing readings first.

**Independent Test**: Run a BMP280-equipped device long enough to accumulate several history samples, then confirm the history view, the JSON records, and the downloaded CSV each carry a pressure value for the new samples.

**Acceptance Scenarios**:

1. **Given** a BMP280-equipped device has been running past several sampling intervals, **When** the owner opens the history view, **Then** recorded entries show pressure together with temperature and timestamp.
2. **Given** history is downloaded as CSV, **When** the file is opened, **Then** it contains a pressure column with values for samples taken while the BMP280 was active.
3. **Given** history records exist from before this feature (temperature-only), **When** the owner views or downloads history, **Then** old records remain intact and readable, with pressure clearly absent rather than shown as zero or a fabricated value.
4. **Given** the retention window fills, **When** the oldest entries are dropped, **Then** pressure history follows the same 3-month retention as temperature.

---

### User Story 3 - The right sensor is picked automatically (Priority: P3)

The same firmware runs on stations with different sensor fittings, with no configuration by the owner. At startup the device detects what is attached: with a BMP280, it supplies both temperature and pressure; with only the wired probe, temperature comes from the probe and pressure surfaces simply don't appear as available; with neither, all readings show unavailable.

**Why this priority**: This protects the existing fleet (probe-only stations must not regress) and makes the hardware upgrade a pure plug-and-reboot operation — but it is the supporting behavior around US1/US2, not the headline capability.

**Independent Test**: Boot the same firmware three times: once with a BMP280, once with only the probe, once with no sensor. Verify respectively temperature+pressure, temperature-only (identical to today's behavior), and unavailable readings — with no settings changed between boots.

**Acceptance Scenarios**:

1. **Given** a BMP280 is attached at power-on, **When** the device boots, **Then** the BMP280 is used for both temperature and pressure, even if the wired probe is also attached.
2. **Given** no BMP280 but a wired probe is attached at power-on, **When** the device boots, **Then** temperature comes from the probe and no pressure reading is offered (screen and web page omit it or mark it unavailable — no blank or bogus values).
3. **Given** neither sensor responds at power-on, **When** the device boots, **Then** temperature and pressure both show the existing "unavailable" indication and the rest of the device (time, web pages, WiFi) works normally.
4. **Given** the owner attaches a BMP280 to a probe-only station, **When** the device is next rebooted, **Then** pressure readings appear without any settings change.

---

### Edge Cases

- Both sensors attached → BMP280 wins for both readings (per detection rule); the probe is ignored while the BMP280 works.
- BMP280 present at boot but failing intermittently afterwards → readings marked unavailable during gaps; the device does not silently switch temperature back to the probe mid-run (detection happens at startup only; a reboot re-detects).
- Out-of-range or physically implausible values (e.g., pressure far outside ~300–1100 hPa) → treated as invalid, shown as unavailable, never recorded to history as real data.
- History spanning a hardware upgrade → older temperature-only records and newer temperature+pressure records coexist in the same view, JSON, and CSV without confusing the reader.
- Temperature unit toggle (°C/°F) → continues to work regardless of which sensor supplies temperature; pressure is unaffected by the toggle.
- Localized pages (feature 004) → every new pressure-related label or status appears in all four supported languages, falling back to English per the existing i18n rules.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST support a BMP280 sensor as a source of both ambient temperature and atmospheric pressure, refreshed at least every 5 seconds (the existing temperature cadence).
- **FR-002**: At startup the system MUST detect attached sensors and select sources automatically: BMP280 present → BMP280 supplies temperature and pressure; otherwise wired probe present → probe supplies temperature and pressure is unavailable; otherwise → both readings unavailable. No user configuration is involved.
- **FR-003**: The built-in screen MUST show the current pressure with its unit whenever a pressure reading is available, alongside the existing time and temperature, and MUST indicate unavailability rather than showing stale or placeholder values.
- **FR-004**: The management page's current readings MUST include pressure when available, refreshing on the same cadence as temperature, and MUST omit or mark it unavailable otherwise.
- **FR-005**: Pressure MUST be sampled into the history log on the same 5-minute schedule and 3-month retention as temperature, and exposed wherever temperature history is exposed (history view, machine-readable records, CSV download).
- **FR-006**: History records created without pressure (pre-upgrade data or probe-only operation) MUST remain valid and clearly distinguish "no pressure recorded" from a measured value.
- **FR-007**: Invalid, missing, or out-of-range sensor values MUST be marked unavailable within 15 seconds, never displayed as current, and never recorded to history as measurements.
- **FR-008**: All existing temperature behavior (°C/°F toggle and its persistence, display layout legibility, unavailable indication) MUST continue to work unchanged regardless of which sensor supplies temperature; probe-only stations MUST behave exactly as before this feature.
- **FR-009**: All new user-visible text introduced for pressure (labels, units context, unavailable states) MUST be localized in the four supported languages per the existing localization behavior.

### Key Entities

- **Pressure reading**: The most recent atmospheric pressure measurement with a validity flag and timestamp; consumed by the display, the management page, and the history recorder. Expressed in hectopascals (hPa).
- **Sensor configuration**: The set of sensors detected at startup and the resulting source assignment (which sensor supplies temperature; whether pressure is available). Fixed until the next reboot.
- **History record (extended)**: Timestamped sample now carrying temperature and, when available, pressure; older temperature-only records remain readable alongside extended ones.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On a BMP280-equipped station, a valid pressure reading appears on the screen and the management page within 10 seconds of power-on (matching the existing temperature target).
- **SC-002**: The displayed pressure agrees with a trusted local reference within ±3 hPa, and follows a real pressure change within 10 seconds.
- **SC-003**: A probe-only station running the new firmware shows zero behavior change: temperature, display, history, and web pages work exactly as before, and no pressure artifacts (blank fields, zeros, error text) appear anywhere.
- **SC-004**: After 24 hours of operation, the history contains a pressure value for at least 95% of the expected 5-minute samples, and the CSV download reproduces them all.
- **SC-005**: Upgrading a station is plug-and-reboot: attaching a BMP280 and power-cycling yields pressure readings with zero configuration steps; removing it and power-cycling returns the station to probe-only behavior.
- **SC-006**: When the active sensor is disconnected mid-run, both affected readings show unavailable within 15 seconds, and no stale or invalid value is added to history during the outage.

## Assumptions

- Pressure is displayed in hectopascals (hPa) with no user-selectable pressure unit; a unit toggle (e.g., mmHg, inHg) can be specified later if wanted.
- Sensor detection happens at startup only; hot-plugging a sensor takes effect at the next reboot. This matches the user's stated intent ("detect which sensor is present on startup").
- When a BMP280 is detected, the wired probe is ignored entirely for as long as the device runs, even if the BMP280 later fails mid-run (no silent mid-run source switching; a reboot re-detects).
- Pressure is reported as measured at the station (station pressure); no sea-level normalization or altitude compensation is applied.
- The existing history storage format can be extended while keeping pre-existing temperature-only records readable; the 3-month retention target is unchanged.
- New pressure labels reuse the localization mechanism delivered by feature 004 (en/de/fr/uk with English fallback).
- The hardware emulator (feature 003) will be extended to simulate pressure readings and both sensor-fitting configurations so the web surfaces can be validated without hardware.
