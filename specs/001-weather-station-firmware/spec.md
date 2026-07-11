# Feature Specification: ESP32 Weather Station Firmware

**Feature Branch**: `001-weather-station-firmware`

**Created**: 2026-07-11

**Status**: Draft

**Input**: User description: "Build firmware for tenstar t-display module with ESP-IDF that: connects to local WiFi network with stored user/pass; if no known AP found create WiFi AP with weather-{last 2 bytes of MAC as hex} name; has captive portal page to setup WiFi user/pass; has dallas ds18b20 temperature sensor; has ST7789V tft display 250x135 px; use freertos tasks: web_server, sensor, display; show current time and temperature on display (landscape); 16MB flash, no PSRAM; support OTA update; support https; custom CA to sign certs for devices on local networks"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - View Time and Temperature at a Glance (Priority: P1)

A person places the weather station device anywhere within reach of power. The device continuously measures the ambient temperature with its wired temperature probe and shows the current time and the current temperature on its built-in screen, oriented in landscape so it is easy to read from a distance.

**Why this priority**: This is the core value of the product — an always-on glanceable display of time and temperature. Without it, nothing else matters.

**Independent Test**: Power the device on with the temperature probe attached. Within a short boot period the screen shows a temperature reading that tracks the real ambient temperature and (once time has been obtained) the current local time, laid out in landscape orientation.

**Acceptance Scenarios**:

1. **Given** the device is powered and the probe is attached, **When** the device finishes booting, **Then** the screen shows the current temperature in landscape orientation.
2. **Given** the device has obtained the current time, **When** the user looks at the screen, **Then** the displayed time matches real local time and updates at least once per minute.
3. **Given** the ambient temperature changes (e.g., the probe is warmed by hand), **When** the user watches the screen, **Then** the displayed temperature follows the change within a few seconds.
4. **Given** the temperature probe is disconnected or fails, **When** the device cannot obtain a reading, **Then** the screen clearly indicates the reading is unavailable instead of showing a stale or bogus value.
5. **Given** the device has not yet obtained the current time, **When** the user looks at the screen, **Then** the time area clearly indicates time is not yet available rather than showing a wrong time.
6. **Given** the device is displaying time, **When** the user presses the left button, **Then** the displayed time toggles between local timezone and UTC, and the active mode is clearly indicated on screen.
7. **Given** the device is displaying temperature, **When** the user presses the right button, **Then** the displayed temperature toggles between Celsius and Fahrenheit, and the active unit is clearly indicated on screen.

---

### User Story 2 - First-Time Network Setup via Captive Portal (Priority: P2)

A new (or factory-reset) device has no saved network credentials. It advertises its own temporary WiFi hotspot named `weather-XXXX`, where `XXXX` uniquely identifies the device (derived from the last two bytes of its hardware address, in hex). The owner connects a phone or laptop to that hotspot and is automatically taken to a setup page (captive portal) where they enter their home network name and password. The device saves the credentials, joins the home network, and stops advertising its own hotspot.

**Why this priority**: Without network onboarding the device cannot obtain the time, be updated, or be reached — but it still shows temperature, so this is second to the display story.

**Independent Test**: Boot a device with no stored credentials, observe the `weather-XXXX` hotspot appear, connect with a phone, complete the portal form with valid home network credentials, and verify the device joins the home network.

**Acceptance Scenarios**:

1. **Given** the device has no stored network credentials, **When** it boots, **Then** within a short period a WiFi hotspot named `weather-` followed by four hex characters unique to the device becomes visible.
2. **Given** a phone connects to the device hotspot, **When** the phone probes for connectivity, **Then** the phone is redirected to the device's setup page without the user typing an address.
3. **Given** the setup page is open, **When** the owner submits a network name and password, **Then** the device stores them, attempts to join that network, and reports success or failure to the user.
4. **Given** the setup page is open, **When** the owner selects a timezone from the portal, **Then** the device saves that timezone and uses it immediately for all time display and calculations.
5. **Given** a client browser with a preferred language of English, German, French, or Ukrainian opens the portal, **When** the page loads, **Then** all portal text is rendered in that language; any other browser language falls back to English.
6. **Given** the owner submitted wrong credentials, **When** the join attempt fails, **Then** the device returns to (or remains in) hotspot mode so the owner can try again, and the portal indicates the previous attempt failed.
7. **Given** setup completed successfully, **When** the device reboots later, **Then** it joins the saved network automatically without re-entering credentials.

---

### User Story 3 - Automatic Reconnection to a Known Network (Priority: P3)

A device that has been set up before boots or loses connectivity. It automatically reconnects to the saved network without any user action. If the saved network is genuinely unavailable (e.g., the router changed), the device falls back to its own setup hotspot so the owner can reconfigure it.

**Why this priority**: Reliability of the connection makes time sync, remote viewing, and updates dependable, building on the onboarding of Story 2.

**Independent Test**: Set up a device, power-cycle it, and verify it rejoins the network unattended. Then disable the network and verify the device eventually offers its setup hotspot again.

**Acceptance Scenarios**:

1. **Given** valid credentials are stored, **When** the device boots, **Then** it connects to the saved network without user interaction.
2. **Given** the device is connected and the network drops briefly, **When** the network returns, **Then** the device reconnects automatically.
3. **Given** the saved network stays unavailable past a reasonable retry period, **When** retries are exhausted, **Then** the device starts its setup hotspot so it can be reconfigured, while continuing to show temperature on screen.

---

### User Story 4 - Secure Remote Access on the Local Network (Priority: P4)

Once the device is on the home network, the owner can open the device's web page from any browser on the same network over an encrypted (HTTPS) connection to view current readings and manage the device. Devices present certificates issued by the owner's own private certificate authority (CA); after the owner installs that CA certificate on their computers/phones once, every device on the network is trusted without per-device security warnings.

**Why this priority**: Remote viewing and encrypted management build on a working, connected device; valuable but not required for the core display function.

**Independent Test**: With the CA certificate installed on a client machine, browse to the device over HTTPS and confirm the connection is reported secure and current readings are shown.

**Acceptance Scenarios**:

1. **Given** the device is on the local network, **When** the owner opens the device address in a browser over HTTPS, **Then** the page loads over an encrypted connection and shows the current temperature.
2. **Given** the owner's private CA certificate is installed on the client device, **When** the owner connects to any weather station signed by that CA, **Then** the browser treats the connection as trusted with no certificate warning.
3. **Given** a repeatable procedure for the owner, **When** a new device is provisioned, **Then** a device certificate signed by the owner's CA can be generated and installed on it.

---

### User Story 5 - Over-the-Air Firmware Update (Priority: P5)

The owner can update the device firmware over the network without connecting cables. The update is delivered over an encrypted connection. If an update fails or the new firmware doesn't start correctly, the device automatically returns to the previous working firmware instead of becoming unusable.

**Why this priority**: Updates protect the long-term investment but are not needed for day-one value.

**Independent Test**: Deliver a new firmware image to a running device over the network, verify the device reboots into the new version, then deliver a deliberately broken image and verify the device recovers to the previous version.

**Acceptance Scenarios**:

1. **Given** the device is on the network, **When** the owner submits a new firmware image through the device's management page, **Then** the device installs it and reboots into the new version.
2. **Given** an update transfer is interrupted (e.g., power loss mid-update), **When** the device restarts, **Then** it boots the previous working firmware and remains fully functional.
3. **Given** a new firmware image fails to start correctly, **When** the failure is detected, **Then** the device rolls back to the previous working firmware automatically.
4. **Given** an update is in progress, **When** the owner looks at the management page, **Then** update progress and the outcome (success/failure) are visible.

---

### User Story 6 - View Temperature History (Priority: P6)

The owner opens the device's management page and can review past temperature readings going back up to three months. The device silently records one reading every five minutes to on-device storage; the owner does not need to do anything to start logging. When the three-month window is full, the oldest entries are dropped automatically to make room for new ones.

**Why this priority**: Useful context for spotting trends or diagnosing heating/cooling issues, but adds no day-one value — all higher-priority stories must work first.

**Independent Test**: Let the device run for a period long enough to accumulate several readings, then open the management page and confirm the recorded readings are present and match the time and approximate temperature values observed during that period.

**Acceptance Scenarios**:

1. **Given** the device has been running and recording readings, **When** the owner opens the management page, **Then** a list or downloadable record of past temperature readings with timestamps is accessible; the same data MUST be available as a JSON endpoint returning an array of `{timestamp, temperature}` records.
2. **Given** readings have been accumulating in memory, **When** one hour elapses, **Then** the buffered readings are appended to persistent storage in a single write, without any owner action.
3. **Given** the log contains entries older than three months, **When** the daily maintenance runs, **Then** all out-of-window entries are removed in a single purge operation so total stored history never exceeds three months.
4. **Given** the device loses power and restarts, **When** the owner checks the log, **Then** all readings recorded before the power loss are still present.

---

### Edge Cases

- Temperature probe disconnected, shorted, or returning out-of-range values → display and web page show "unavailable" state; normal display resumes when readings return.
- Device boots with no network available at all → temperature display still works; time shows "not available" until a time source is reached.
- Time was obtained, then network is lost for an extended period → device keeps counting time internally and continues to display it (accuracy may drift until reconnection).
- Owner enters a network name that is momentarily out of range during setup → device reports the failure and allows retry rather than silently saving bad credentials.
- Two setup clients connect to the hotspot at once → the portal remains usable; last submitted credentials win.
- Firmware image offered for update is not a valid image for this device → update is rejected before installation; running firmware is untouched.
- Power loss at any moment (including mid-update and mid-settings-save) → device always boots into a working firmware with either the old or new settings, never a corrupted half-state.
- Hotspot name collision is effectively avoided because the suffix derives from the device's unique hardware address.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST continuously measure ambient temperature via the attached wired probe and refresh the reading at least every 5 seconds.
- **FR-002**: System MUST show the current temperature and current local time on the built-in screen in landscape orientation, updating the temperature within 5 seconds of a new reading and the time at least every minute.
- **FR-003**: System MUST clearly indicate on the screen when the temperature reading or the current time is unavailable, never displaying stale data as if current.
- **FR-004**: System MUST persistently store one set of WiFi network credentials (network name and password) across power cycles.
- **FR-005**: On startup with stored credentials, system MUST attempt to join the stored network automatically and MUST reconnect automatically after transient network loss.
- **FR-006**: When no stored credentials exist, or the stored network cannot be joined after a bounded retry period, system MUST start its own open setup hotspot named `weather-` followed by the last two bytes of the device hardware address rendered as four lowercase hex characters.
- **FR-007**: While in hotspot mode, system MUST present a captive portal: clients connecting to the hotspot are automatically directed to a setup page without entering an address.
- **FR-008**: The setup page MUST allow the user to enter a network name and password, MUST allow the user to select the device timezone, MUST report whether the subsequent join attempt succeeded, and on failure MUST allow the user to retry.
- **FR-009**: After successfully joining a network via the portal, system MUST save the credentials and stop the setup hotspot.
- **FR-010**: Once on a network, system MUST obtain the current local time from a network time source and keep it updated; between updates it MUST keep time internally.
- **FR-011**: System MUST serve a management web page over HTTPS on the local network showing the current temperature and device status.
- **FR-012**: HTTPS service MUST use a per-device certificate signed by an owner-operated private certificate authority; the project MUST include a documented, repeatable procedure to create that CA and to issue and install device certificates.
- **FR-013**: Clients that have installed the owner's CA certificate MUST be able to connect to any device so provisioned without certificate trust warnings.
- **FR-014**: System MUST accept firmware updates over the network via an encrypted connection, delivered through the management page.
- **FR-015**: System MUST validate an offered update image before applying it and MUST reject invalid images without affecting the running firmware.
- **FR-016**: System MUST survive an interrupted or failed update by booting the last known-good firmware automatically (rollback), with no user intervention.
- **FR-017**: System MUST report update progress and outcome to the user performing the update.
- **FR-018**: Sensor reading, display rendering, and web serving MUST operate concurrently so that a slow or stalled network never freezes the display or sensor updates.
- **FR-019**: All persistent settings writes MUST be atomic with respect to power loss: after any power cut, the device holds either the previous or the new value, never a corrupted one.
- **FR-020**: System MUST fit entirely within the device's onboard resources (16 MB flash storage, no external RAM), including space reserved for holding two firmware versions to support safe updates.
- **FR-021**: The clock MUST apply the configured timezone including automatic daylight saving time (summer time) transitions; the displayed local time and all time-based logic MUST reflect DST offsets without manual adjustment.
- **FR-022**: The captive portal setup page MUST be fully localized in English (en), German (de), French (fr), and Ukrainian (uk). The displayed language MUST be selected automatically from the connecting client's `Accept-Language` HTTP header, with English as the fallback when no supported language matches.
- **FR-023**: System MUST sample temperature at 5-minute intervals and buffer readings in RAM; buffered readings MUST be appended to persistent storage once per hour (one write per hour). Once per day, all entries older than 3 months MUST be purged in a single operation. Total retained history MUST cover at least the last 3 months (≈ 26 000 readings).
- **FR-024**: The management web page MUST expose the stored temperature log so the owner can view or download historical readings with their timestamps; the log MUST also be accessible as a JSON endpoint returning an array of `{timestamp, temperature}` records.
- **FR-025**: The left hardware button MUST toggle the display time between local timezone and UTC on each press; the currently active mode MUST be visible on screen. The selected mode MUST persist across reboots.
- **FR-026**: The right hardware button MUST toggle the display temperature unit between Celsius and Fahrenheit on each press; the currently active unit MUST be visible on screen. The selected unit MUST persist across reboots.

### Key Entities

- **Network Credentials**: One stored record of network name and password; created/replaced via the setup portal; survives reboot and firmware updates.
- **Temperature Reading**: The most recent probe measurement with a validity flag (valid / unavailable); consumed by the display and the web page.
- **Device Identity**: The stable unique hardware address of the device; its last two bytes name the setup hotspot; the full identity is embedded in the device certificate.
- **Device Certificate & Private CA**: An owner-operated certificate authority and the per-device certificates it issues; establish trusted encrypted connections on the local network.
- **Firmware Image**: A versioned, verifiable update package; at any time the device holds a running image and a fallback image.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: From power-on, a configured device shows a valid temperature on screen within 10 seconds.
- **SC-002**: The displayed temperature tracks a real ambient change within 10 seconds of the change reaching the probe.
- **SC-003**: A first-time owner with a phone completes network setup via the captive portal in under 3 minutes without instructions beyond the portal page itself.
- **SC-004**: After a router power-cycle, a configured device is back on the network without user action within 60 seconds of the network returning.
- **SC-005**: With the owner's CA installed, 100% of connections to the device's web page are established encrypted and without any browser trust warning.
- **SC-006**: A firmware update over the network completes in under 5 minutes, and 100% of failed or interrupted updates leave the device running the previous firmware.
- **SC-007**: The device runs continuously for at least 7 days without a visible freeze of the display, missed sensor updates, or loss of web access.
- **SC-008**: Once time has been obtained, displayed time stays within 2 seconds of true local time while the device remains network-connected.
- **SC-009**: After 3 months of continuous operation, at least 26 000 timestamped readings are present in the log and accessible from the management page, with no reading gap longer than 10 minutes under normal operating conditions.

## Assumptions

- The device is the Tenstar T-Display-style module (16 MB flash, no external RAM) with a 250×135 color screen, two push buttons, and a wired digital temperature probe on dedicated pins, as itemized in the user input; the pin assignments given there are authoritative for planning.
- Temperature is displayed in degrees Celsius with 0.1° resolution by default; the right button toggles between Celsius and Fahrenheit.
- Current time comes from a network time source once connected; there is no battery-backed clock, so time is "unavailable" after boot until first sync. Timezone is a device setting with a sensible default (UTC), selectable by the user in both the captive portal setup page and the management page; the timezone rule includes automatic daylight saving time (summer time) transitions.
- The setup hotspot is open (no password): it exists only briefly for onboarding, carries no secrets from the device, and openness is what makes captive-portal redirection work smoothly across phones.
- The two hardware buttons are assigned in v1: left button toggles displayed time between local timezone and UTC; right button toggles displayed temperature unit between Celsius and Fahrenheit. Both selections persist across reboots.
- The management page in v1 covers: current temperature, device status (network, firmware version, time sync), timezone setting, firmware update upload, and temperature history log (view/download). Graphical visualisation of the temperature log is out of scope for v1.
- OTA updates are initiated by the owner from the management page (manual upload); automatic update checking against a remote server is out of scope for v1.
- The private CA and certificate issuance happen on the owner's computer as a documented tooling/procedure deliverable; the device only stores its own certificate and key. CA private key never resides on a device.
- Single stored network profile is sufficient for v1 (no list of multiple known networks).
- Localization applies to the captive portal only in v1; the management page is English-only.
- The device is used indoors on a home/small-office network; no cloud connectivity is required or provided in v1.
