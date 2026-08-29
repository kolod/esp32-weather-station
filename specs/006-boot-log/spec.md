# Feature Specification: Boot Log

**Feature Branch**: `006-boot-log`

**Created**: 2026-07-15

**Status**: Draft

**Input**: User description: "save boot debug messages in file `boot.log` and add it to web interface (for better debugging sensors/display init)"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - View boot diagnostics from management page (Priority: P1)

After the device powers on, a developer opens the management web interface and reads the startup diagnostic log to understand what happened during boot — which sensor was detected, whether the display initialized, and whether any errors occurred — without needing a serial cable.

**Why this priority**: The core value of the feature is removing the need for a serial monitor to diagnose boot problems. This story delivers that value in full.

**Independent Test**: Start the device, navigate to the management page, and verify that a boot log section is visible and shows sensor detection and display initialization results from the most recent boot.

**Acceptance Scenarios**:

1. **Given** the device has completed its boot sequence, **When** the developer opens the management page, **Then** a boot log section displays the diagnostic messages captured during startup.
2. **Given** the boot log section is visible, **When** the developer reads the log, **Then** the log clearly shows whether each sensor was detected and whether the display initialized successfully.
3. **Given** a sensor detection failure occurred at boot, **When** the developer reads the boot log, **Then** the failure reason is present in the log (e.g., "BMP280 not found at 0x76 or 0x77").
4. **Given** the device has rebooted, **When** the developer views the boot log, **Then** the log reflects the most recent boot only (not a mix of prior boots).

---

### User Story 2 - Download boot log as a file (Priority: P2)

A developer downloads the raw boot log as a plain-text file to share with others, attach to a bug report, or inspect offline with tools of their choice.

**Why this priority**: Augments US1 — viewing is sufficient for quick diagnosis, but download enables sharing and deeper analysis without copy-paste.

**Independent Test**: Click a "Download" link/button in the boot log section and verify a plain-text file is saved containing the boot log content.

**Acceptance Scenarios**:

1. **Given** the boot log section is visible, **When** the developer clicks "Download", **Then** a plain-text file named `boot.log` is saved to the developer's computer.
2. **Given** the download is requested, **When** the file is opened, **Then** it contains the same content as displayed in the web interface.

---

### User Story 3 - Boot log persists through network-up delay (Priority: P3)

Sensor detection and display initialization happen early in the boot sequence — before the network is up and the web server is reachable. The developer can still retrieve the full diagnostic log once the device comes online, even though the log was written minutes earlier.

**Why this priority**: Without persistence, the log would only be available if the web server were running at the moment the boot events occurred — which it is not. This story ensures the log survives the gap between boot events and network readiness.

**Independent Test**: Power-cycle the device, wait for the network and web server to become available, then retrieve the boot log and confirm it contains events from early boot (sensor/display init), not just post-network events.

**Acceptance Scenarios**:

1. **Given** sensor detection and display init occurred before the web server started, **When** the developer retrieves the boot log after the device is fully online, **Then** the log contains the early-boot diagnostic messages.
2. **Given** the storage subsystem was unavailable at the moment of a log write attempt, **When** the device eventually completes boot, **Then** the device operates normally (boot-log unavailability does not cause a failure).

---

### Edge Cases

- What if storage is full when the boot log is written? Boot continues normally; log write is silently skipped.
- What if storage is not yet mounted when early boot messages are emitted? Messages before storage mount are not captured; this is acceptable since the storage mount itself is not a diagnostic target.
- What if the device reboots before the log is fully written? The partial log from the new boot replaces the old one; no corruption of other files occurs.
- What if the boot log file is missing when the management page is loaded? The log section displays a "not available" message rather than an error.
- How large can the log grow? The log file is capped at a fixed maximum size; messages beyond the cap are truncated rather than wrapping to avoid overwriting earlier entries.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The device MUST capture diagnostic messages emitted by the sensor detection subsystem during startup and write them to a persistent log file named `boot.log`.
- **FR-002**: The device MUST capture diagnostic messages emitted by the display initialization subsystem during startup and write them to the same `boot.log` file.
- **FR-003**: The `boot.log` file MUST be overwritten at the start of each new boot, so it always reflects only the most recent boot sequence.
- **FR-004**: The `boot.log` file MUST have a fixed maximum size; when the maximum is reached, further messages are discarded rather than wrapping.
- **FR-005**: The management web interface MUST expose the `boot.log` content in a dedicated section on the main page, displaying it as preformatted text.
- **FR-006**: The management web interface MUST provide a link or button to download `boot.log` as a plain-text file.
- **FR-007**: If the log file is absent or unreadable, the management page MUST display an appropriate "not available" message in the boot log section rather than failing to load.
- **FR-008**: Boot-log write failures MUST NOT affect the normal boot sequence; the device MUST continue booting regardless of log persistence outcome.
- **FR-009**: The boot log section in the management interface MUST have a localizable heading (added to all language packs).

### Key Entities

- **Boot Log File**: A plain-text, newline-delimited diagnostic file written once per boot. Bounded in size. Overwritten on each boot. Contains timestamped-or-sequenced log lines from sensor detection and display initialization.
- **Boot Log Section**: The UI region on the management page that displays the boot log content and offers a download action.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A developer can determine, without a serial cable, why a sensor was not detected — by reading the boot log in the management interface — in under 60 seconds after the device comes online.
- **SC-002**: The boot log is retrievable via the management interface within the normal page-load time after the device is reachable on the network.
- **SC-003**: The boot log file never exceeds 4 KB, ensuring it cannot meaningfully contribute to storage exhaustion on the device's limited flash filesystem.
- **SC-004**: All existing automated tests (emulator suite, i18n checker, firmware build) continue to pass after this feature is implemented — no regressions.
- **SC-005**: The boot log section heading is correctly localized in all four supported languages (English, German, French, Ukrainian).

## Assumptions

- The log captures messages from sensor detection and display initialization only, not all firmware subsystems. Limiting scope keeps the file small and purposeful.
- The log is reset (overwritten) on each boot. Append-across-reboots would grow unboundedly and make it harder to isolate a specific boot's behavior.
- Log messages include all severity levels (info, warning, error) so successful detections are also visible, not just failures.
- The maximum file size is 4 KB — large enough for typical boot output (~50–100 lines) and small enough to be negligible on the flash filesystem.
- The boot log is read-only from the web interface; there is no UI action to clear or reset it (a reboot already resets it).
- The boot log feature is scoped to the management interface only, not the captive portal.
- The hardware emulator (`tools/hw_emulator.py`) will serve a synthetic boot log at the same endpoint so automated tests can cover the HTTP contract without hardware.
