# Feature Specification: Local Hardware Emulator for Web UI Testing

**Feature Branch**: `003-hardware-emulator`

**Created**: 2026-07-12

**Status**: Draft

**Input**: User description: "create python script that run local web server that emulate real hardware to speedup testing of web page (mgmt & portal)"

## Clarifications

### Session 2026-07-12

- Q: Should the emulator open the pages automatically after startup? → A: Yes — after both endpoints are up, open the management page and the portal page in the developer's default browser (user directive).
- Q: How should the developer select failure/edge scenarios (FR-010)? → A: Both — startup options set the initial scenario state, and scenarios can also be switched at runtime without restarting the emulator.
- Q: Should the emulator be reachable from other devices on the local network? → A: No — localhost only; the pages are reachable solely from the developer's own machine.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Develop the Management UI Without a Device (Priority: P1)

A developer working on the management web page (status dashboard, settings, history chart, firmware update) starts the emulator with a single command on their PC, opens the management page in a browser, and sees it fully working against realistic simulated device data. When they edit the page's source files (HTML/CSS/JS), a simple browser refresh shows the change — no firmware rebuild, no flashing, no physical device.

**Why this priority**: The management page is the most feature-rich UI and currently every tweak requires a full firmware build + flash cycle (minutes per iteration). Removing the device from the loop is the core value of this feature.

**Independent Test**: Start the emulator, open the management page in a browser, verify the status dashboard populates with simulated values, change a visible label in the page source, refresh, and confirm the change appears without restarting the emulator.

**Acceptance Scenarios**:

1. **Given** the emulator is running, **When** the developer opens the management page URL in a browser, **Then** the page loads and displays simulated temperature, time/sync status, WiFi info, firmware version, uptime, history count, and storage figures.
2. **Given** the management page is open, **When** the developer changes settings (timezone, time mode, temperature unit) and saves, **Then** the emulator accepts the change and subsequent status responses reflect the new settings for the rest of the session.
3. **Given** the management page is open, **When** the developer views the temperature history, **Then** a plausible synthetic history dataset is shown, and the CSV download returns the same data in CSV form.
4. **Given** the developer edits a management page source file, **When** they refresh the browser, **Then** the updated file is served immediately without restarting the emulator or rebuilding anything.
5. **Given** the management page is open, **When** the developer submits an invalid settings value (e.g., unknown timezone), **Then** the emulator rejects it with the same error behavior the device produces, so the page's error handling can be tested.
6. **Given** the emulator is started, **When** both endpoints become reachable, **Then** the management page and the portal page open automatically in the developer's default browser.

---

### User Story 2 - Exercise the Captive Portal Provisioning Flow (Priority: P2)

A developer working on the WiFi provisioning (captive portal) page starts the emulator, opens the portal page, and walks through the full provisioning journey: viewing a list of nearby networks, picking one, entering a password, and watching the connection progress through connecting / connected / failed states — all simulated, repeatable on demand.

**Why this priority**: The portal flow is stateful (scan → join → poll status) and its failure paths (wrong password, network out of range) are hard to reproduce with real hardware. Second priority because the portal page is simpler than the management page but still needs iteration speed.

**Independent Test**: Start the emulator, open the portal page, complete a join with a "good" network and see it reach the connected state; then join with a "wrong password" network and see the authentication-failure message.

**Acceptance Scenarios**:

1. **Given** the portal page is open, **When** the developer requests a network scan, **Then** a realistic list of networks (varied names, signal strengths, security types) is returned.
2. **Given** a network is selected and credentials submitted, **When** the page polls the connection status, **Then** the status progresses from connecting to connected (including the device-name suffix the real device reports) after a short simulated delay.
3. **Given** the developer chooses a scenario representing wrong credentials, **When** the join is attempted, **Then** the status ends in failed with the authentication-failure reason, matching real device behavior.
4. **Given** the developer chooses a scenario representing an unreachable network, **When** the join is attempted, **Then** the status ends in failed with the network-not-found reason.
5. **Given** a browser or OS issues a captive-portal detection probe request, **When** the emulator receives it, **Then** it responds the same way the device does (redirecting to the portal page), so the captive-portal experience can be verified.

---

### User Story 3 - Simulate Firmware Update and Edge Conditions (Priority: P3)

A developer testing the firmware-update section of the management page uploads a file through the page and watches the update progress and result — success or a chosen failure — without risking a real device. They can also put the emulated device into edge states (sensor fault, time never synced, WiFi disconnected, storage nearly full) to check how the pages present them.

**Why this priority**: These states are rare or risky to produce on real hardware, but the pages must render them correctly. Valuable, but the pages are usable without it.

**Independent Test**: Upload any file via the management page's update control and observe simulated progress ending in success; re-run with a failure scenario selected and observe the page's error presentation.

**Acceptance Scenarios**:

1. **Given** the management page is open, **When** the developer uploads a file as a firmware update, **Then** the emulator reports realistic progress over time ending in a success state.
2. **Given** a failure scenario is selected, **When** an update is attempted, **Then** the emulator reports the corresponding failure state so the page's error path can be observed.
3. **Given** an edge-state scenario is selected (e.g., sensor reading invalid or time not yet synchronized), **When** the management page loads status, **Then** the simulated data reflects that state and the page's handling can be verified.

---

### Edge Cases

- What happens when the emulator's port is already in use on the developer's machine? The emulator must fail with a clear message (or allow choosing another port) rather than appearing to start.
- What happens when the page source files cannot be found (script run from the wrong directory)? The emulator must report which path it expected instead of serving empty responses.
- How does the emulator behave when the browser requests a language the device supports (via the browser's language preferences)? It must select the language the same way the device does, so translated UI states can be tested.
- What happens when the page requests an endpoint the emulator does not know? It must return a not-found response rather than crashing, and log the miss so gaps in emulation are visible.
- Concurrent requests from multiple browser tabs must not corrupt the simulated state (e.g., two tabs polling status during a join flow).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The emulator MUST run entirely on the developer's machine and require no physical device, no firmware build, and no network beyond the local machine; it MUST accept connections only from the local machine (localhost), never from other devices on the network.
- **FR-002**: The emulator MUST serve both the management page and the provisioning portal page simultaneously, each reachable at its own local address, so both UIs can be tested side by side.
- **FR-003**: The emulator MUST serve the page assets (HTML, CSS, JS, translation files) directly from their source locations in the repository, re-reading them on every request so edits are visible on browser refresh without restart.
- **FR-004**: The emulator MUST implement every data interaction the management page performs: current status, settings changes, temperature history retrieval, history CSV export, timezone catalog, firmware-update upload, and firmware-update progress — with response shapes matching the real device.
- **FR-005**: The emulator MUST implement every data interaction the portal page performs: network scan, join request, join-status polling, and timezone catalog — with response shapes matching the real device.
- **FR-006**: The emulator MUST answer captive-portal detection probe requests the same way the device does (redirect to the portal page).
- **FR-007**: The emulator MUST maintain session state: settings changes persist for the life of the emulator run and are reflected in later status responses; a join request drives the join status through the same state sequence the device produces.
- **FR-008**: The emulator MUST provide realistic simulated data: a temperature value that varies over time, a synthetic history dataset covering a meaningful time span, a varied WiFi scan list, and plausible device metadata (firmware version, uptime that increases, storage figures).
- **FR-009**: The emulator MUST reproduce the device's error responses for invalid inputs (e.g., unknown timezone, invalid setting values) so the pages' error handling can be tested.
- **FR-010**: The emulator MUST let the developer select failure and edge scenarios (at minimum: join wrong-password, join network-not-found, update failure, sensor-invalid, time-not-synced) without editing its code: startup options set the initial scenario state, and scenarios MUST also be switchable at runtime without restarting the emulator.
- **FR-011**: The emulator MUST select the UI language from the browser's language preferences using the same rules as the device (supported languages with fallback to the default).
- **FR-012**: The emulator MUST log each request it serves (and any unmatched request) so developers can see what the pages are calling.
- **FR-013**: The emulator MUST start with a single command and report the addresses where each page is reachable.
- **FR-014**: After startup, once both endpoints are reachable, the emulator MUST automatically open the management page and the portal page in the developer's default browser.

### Key Entities

- **Emulated device state**: The in-memory model of one device for the session — current settings (timezone, time mode, temperature unit), current sensor reading and validity, time-sync state and source, WiFi connection state, uptime, storage figures, firmware version.
- **History dataset**: Synthetic time-stamped temperature records spanning a configurable period, retrievable as structured data or CSV.
- **Network scan list**: Simulated nearby WiFi networks with name, signal strength, and security type; includes designated entries that trigger success, wrong-password, and not-found join outcomes.
- **Join session**: The state machine of one provisioning attempt (connecting → connected or failed with reason), advanced over simulated time.
- **Update session**: The state machine of one firmware-update attempt (receiving → in-progress → success or selected failure).
- **Scenario selection**: The developer-chosen set of edge/failure behaviors active for the session; initialized at startup and changeable at runtime without restart.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A developer with the repository checked out can go from "nothing running" to both pages open and interactive in a browser in under 30 seconds, with one command and no device attached.
- **SC-002**: The edit-to-verify cycle for a page change (edit file → refresh browser → see change) takes under 10 seconds, compared to minutes for a build-and-flash cycle.
- **SC-003**: 100% of the data interactions the management and portal pages perform are answered by the emulator with device-equivalent responses (no browser console errors caused by missing or malformed emulator responses during normal use of either page).
- **SC-004**: Each provisioning outcome (success, wrong password, network not found) and each update outcome (success, failure) can be reproduced on demand in under 1 minute each.
- **SC-005**: All supported UI languages can be visually verified on both pages using only browser language settings, with no emulator restart.

## Assumptions

- The emulator is a development aid for contributors to this repository; it is not shipped to end users and does not need authentication, encryption, or hardening beyond its localhost-only listening rule (FR-001). Testing the pages from other devices (e.g., a phone) is out of scope.
- The user requested a Python script; a stock Python 3 installation on the developer's machine is an acceptable prerequisite, and no device toolchain is required to run it.
- The management and portal experiences run as two simultaneous local endpoints on different ports (the real device runs them at different times on port 80); page code paths that assume same-origin requests continue to work because each page talks only to the endpoint that served it.
- Simulated state is in-memory only and resets when the emulator restarts; persisting state across runs is out of scope.
- Emulating the device's display, buttons, sensor hardware, or non-web behavior is out of scope — only the web-facing behavior is emulated.
- Fidelity target is behavioral equivalence of responses (shapes, states, error codes) as currently implemented in the firmware's web component; when firmware endpoints change, the emulator is updated as part of that change.
- The synthetic history dataset defaults to roughly the retention span the device itself keeps, so charts render with realistic density.
