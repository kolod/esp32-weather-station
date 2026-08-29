# Feature Specification: WiFi Details Screen

**Feature Branch**: `008-wifi-details-screen`

**Created**: 2026-08-29

**Status**: Draft

**Input**: User description: "add new button action right button long press - show wifi datails: ap name, ip address , local dns name"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Check how to reach the device on the network (Priority: P1)

A person standing in front of the weather station wants to open its web interface but does not know which network the device joined or what address to type. They press and hold the right button. The display replaces the normal weather readings with a WiFi details screen showing the name of the network the device is on, its IP address, and its local hostname (the friendly `.local` name). They note the address, then the screen returns to the normal weather view on its own.

**Why this priority**: This is the entire purpose of the feature — giving the user the connection information they need without a serial cable, a router admin page, or a network scanner. Implementing just this story delivers the full value.

**Independent Test**: With the device connected to a WiFi network, long-press the right button and confirm the display shows the joined network name, the current IP address, and the local hostname, then confirm the display reverts to the weather view without further input.

**Acceptance Scenarios**:

1. **Given** the device is connected to a home WiFi network, **When** the user long-presses the right button, **Then** the display shows the joined network name, the device's IP address on that network, and the device's local hostname.
2. **Given** the WiFi details screen is showing, **When** the user takes no further action, **Then** the display automatically returns to the normal weather view after a short, fixed interval.
3. **Given** the WiFi details screen is showing, **When** the user long-presses the right button again, **Then** the display returns to the normal weather view immediately.
4. **Given** the WiFi details screen is showing, **When** a new sensor reading or clock update arrives, **Then** the details screen stays visible (the update is applied underneath and shown once the weather view returns).

---

### User Story 2 - Find the device while it is in access-point fallback mode (Priority: P2)

The device could not join a stored network and has fallen back to hosting its own access point. A person wants to connect a phone to that access point and open the setup portal. They long-press the right button and the display shows the access-point name the device is broadcasting, the address to open in a browser once connected, and the local hostname.

**Why this priority**: Access-point fallback is exactly the situation where the user has no other way to discover the device's name or address, so surfacing it on the display is highly valuable — but it builds on the same screen delivered in User Story 1.

**Independent Test**: Force the device into access-point fallback (no stored credentials), long-press the right button, and confirm the screen shows the broadcast access-point name, the portal address, and the local hostname.

**Acceptance Scenarios**:

1. **Given** the device is in access-point fallback mode, **When** the user long-presses the right button, **Then** the display shows the broadcast access-point name, the access-point IP address, and the local hostname.
2. **Given** the device is still trying to connect and has no IP address yet, **When** the user long-presses the right button, **Then** the display shows the network name it is attempting to join and a clear placeholder in place of the IP address.

---

### Edge Cases

- **No WiFi information available yet** (very early boot, radio initializing): the screen shows a placeholder (e.g. "---") for each unknown field rather than blank space or stale data.
- **Values too long for the panel**: the network name, IP address, and hostname are each rendered so they remain readable on the 250×135 display (truncated with an ellipsis or shrunk to fit rather than overflowing the screen).
- **Rapid repeated long-presses**: toggling the screen on and off quickly never leaves the display stuck on the details screen or blank; the auto-return timer is restarted or cancelled cleanly on each press.
- **Right-button short click while details screen is showing**: the short-click action (temperature unit toggle) still takes effect; the change is visible when the weather view returns. The details screen itself shows no temperature, so there is nothing to update on-screen.
- **WiFi state changes while the screen is showing** (e.g. connection drops, or STA connects and AP shuts down): the screen either reflects the new state at the next redraw or remains until the auto-return timer expires; it never shows a mix of AP and STA data.
- **Left button pressed while details screen is showing**: the normal left-button actions (time mode toggle, factory-reset long-press) continue to work unchanged.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The device MUST detect a long-press of the right button and, in response, replace the normal weather view on the display with a WiFi details screen.
- **FR-002**: The WiFi details screen MUST display the network name the device is associated with: the joined network's name when connected as a client, or the broadcast access-point name when in access-point fallback mode.
- **FR-003**: The WiFi details screen MUST display the device's current IPv4 address on the active interface (the client address when connected, or the access-point address when in fallback mode).
- **FR-004**: The WiFi details screen MUST display the device's local hostname in the form users can type into a browser (the `<hostname>.local` mDNS name).
- **FR-005**: Each field on the WiFi details screen MUST have a short label so the user can tell which value is the network name, which is the address, and which is the hostname.
- **FR-006**: When any field's value is not yet known, the WiFi details screen MUST show a placeholder for that field rather than blank space or a previous value.
- **FR-007**: The WiFi details screen MUST automatically dismiss and return to the normal weather view after a fixed timeout with no user interaction.
- **FR-008**: A second long-press of the right button while the WiFi details screen is showing MUST return the display to the normal weather view immediately.
- **FR-009**: The right button's existing short-click action (temperature unit toggle) and both left-button actions (time mode toggle, factory-reset long-press) MUST continue to work unchanged whether or not the WiFi details screen is showing.
- **FR-010**: Sensor readings, clock updates, and WiFi state changes that arrive while the WiFi details screen is showing MUST NOT be lost; they MUST be reflected in the weather view once it returns.
- **FR-011**: Long field values MUST be rendered to remain fully readable within the panel bounds (truncated or scaled to fit), never overflowing or corrupting the layout.
- **FR-012**: The WiFi details screen MUST NOT introduce blocking delays in the display refresh loop, the sensor sampling task, or the web server task.

### Key Entities

- **WiFi Details View**: A transient full-screen replacement for the weather view. Presents three labelled read-only values — network name, IPv4 address, local hostname — derived from the current WiFi state. Has no editable fields and no navigation beyond dismissal.
- **WiFi Connection Info**: The current, already-tracked WiFi facts the screen reads: active mode (client vs. access-point fallback), associated / broadcast network name, active IPv4 address, and mDNS hostname.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user who does not know the device's address can obtain a working way to reach its web interface (IP address or `.local` name) using only the right button, in under 10 seconds and with no other tools.
- **SC-002**: The WiFi details screen appears within 1 second of completing the long-press.
- **SC-003**: The WiFi details screen returns to the normal weather view automatically within the fixed timeout (target: 10 seconds) when the user does nothing.
- **SC-004**: In access-point fallback mode, the name and address shown on the screen match the access point the device is actually broadcasting, so a user can connect a phone and open the portal on the first attempt.
- **SC-005**: All existing automated tests (emulator suite, i18n checker, firmware build) continue to pass, and no existing button behaviour regresses.
- **SC-006**: Displaying the screen has no measurable effect on sensor sampling cadence or web interface responsiveness.

## Assumptions

- "AP name" means the WiFi SSID: the SSID of the joined network when the device is a client, and the device's own broadcast SSID (`weather-XXXX`) when in access-point fallback mode. Showing the client SSID (rather than only the fallback SSID) is the more useful default and covers the common case.
- "Local DNS name" means the device's mDNS hostname (`weather-XXXX.local`) already registered by the firmware, not a router-assigned DHCP hostname or any externally resolvable DNS record.
- "IP address" means the device's own IPv4 address on the active interface. IPv6, gateway, and DNS-server addresses are out of scope for this screen.
- The long-press uses the same button driver and default long-press timing already used for the left button's factory-reset long-press; no new configuration option is added for the duration.
- The screen is a display-only feature. It is not added to the web interface, and no new persisted setting is introduced.
- The screen auto-returns after a fixed ~10-second timeout; the exact value is a tuning detail for planning and does not need product sign-off.
- While the details screen is showing, incoming updates continue to be recorded in shared application state as they are today; only the on-screen rendering is deferred until the weather view returns.
- The three values are shown as plain text with short labels; no QR code, no icons beyond what the existing UI style uses, and no live-updating fields on this screen.
- The feature targets the existing ST7789 250×135 panel and the existing left/right button hardware; no hardware change is implied.
