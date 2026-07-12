# Feature Specification: RTC Time Restore After Reboot

**Feature Branch**: `002-rtc-time-restore`

**Created**: 2026-07-12

**Status**: Draft

**Input**: User description: "Add RTC support to restore time after reboot if ntp server not respouse or wifi conection not present."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Correct Time Immediately After Reboot Without Network (Priority: P1)

The device owner experiences a power interruption (or the device restarts for any reason) while the home network is down or the internet time service is unreachable. When the device powers back up, it immediately shows the correct current time again — restored from its battery-backed clock — instead of showing "time not available" until the network returns.

**Why this priority**: This is the entire point of the feature. Today, any reboot without network connectivity leaves the clock blank indefinitely, making the device fail at its primary job (a glanceable time and temperature display).

**Independent Test**: Let the device obtain the time once, then disconnect it from all networks, power-cycle it, and confirm the correct local time appears on screen shortly after boot without any network connection present.

**Acceptance Scenarios**:

1. **Given** the device previously had the correct time and holds it in its battery-backed clock, **When** the device is power-cycled with no WiFi network available, **Then** the screen shows the correct local time within the normal boot period without waiting for any network.
2. **Given** the device previously had the correct time, **When** the device reboots with WiFi available but the network time service unreachable, **Then** the screen shows the correct local time restored from the battery-backed clock and keeps counting normally.
3. **Given** the device restored time from the battery-backed clock, **When** the user toggles between local time and UTC with the left button, **Then** both modes display correctly based on the restored time.
4. **Given** the device restored time from the battery-backed clock, **When** temperature readings are recorded, **Then** their timestamps reflect the restored (correct) time rather than being missing or epoch-zero.

---

### User Story 2 - Battery-Backed Clock Stays Synchronized With Network Time (Priority: P2)

While the device is online, it keeps its battery-backed clock aligned with network time automatically, so that whenever the next outage or reboot happens, the stored time is accurate and ready to be restored.

**Why this priority**: The restore in Story 1 is only as good as the time stored in the clock. Without ongoing synchronization, the restored time would drift further from reality after every outage cycle.

**Independent Test**: Deliberately set the battery-backed clock wrong (or let it drift), let the device connect to the network and sync time, then reboot offline and confirm the restored time matches real time — proving the network sync refreshed the stored clock.

**Acceptance Scenarios**:

1. **Given** the device is connected and obtains time from the network time source, **When** the synchronization succeeds, **Then** the battery-backed clock is updated to match the network time.
2. **Given** the battery-backed clock holds an incorrect time and the device boots with network available, **When** network time synchronization succeeds, **Then** the displayed time and the stored clock are both corrected to network time (network time is always authoritative).
3. **Given** the device stays online for an extended period, **When** periodic network time updates occur, **Then** the battery-backed clock is refreshed so its stored time never lags significantly behind real time.

---

### User Story 3 - Graceful Behavior When the Backup Clock Is Unusable (Priority: P3)

On the very first boot, after the backup battery has died, or if the clock hardware fails, the device cannot trust the stored time. It behaves exactly as it does today — showing that time is not yet available and waiting for network time — and everything else (temperature display, setup portal, management page) keeps working normally. The owner can see the state of the time source on the management page.

**Why this priority**: Failure handling protects the trustworthiness of the display ("never show a wrong time as if correct") but only matters in less common situations.

**Independent Test**: Remove or invalidate the backup clock (boot the module with no battery connected), boot without network, and confirm the time area shows "not available", the temperature display still works, and the management page reports the time-source state.

**Acceptance Scenarios**:

1. **Given** the battery-backed clock holds no valid time (first boot or dead battery), **When** the device boots without network, **Then** the time area indicates time is not available and no incorrect time is displayed.
2. **Given** the battery-backed clock reports an implausible time (e.g., a date before the device firmware was released), **When** the device boots, **Then** the stored time is rejected and treated as "no valid time".
3. **Given** the module battery is absent or fully depleted and the RTC domain lost power during the last power-off, **When** the device boots, **Then** the device still starts normally, all other functions work, and time behaves as if no backup clock were present.
4. **Given** any of the above failure states, **When** the owner opens the management page, **Then** the device status shows where the current time came from (network, backup clock, or not available).

---

### Edge Cases

- Backup battery dies while the device is unpowered → stored time is lost or invalid; device MUST detect this (see Story 3) rather than restoring a bogus time.
- Stored time is valid-looking but wrong (e.g., clock was never synced after a battery swap) → plausibility check limits the damage; network sync corrects it as soon as connectivity returns.
- Network time arrives after time was already restored from the backup clock → displayed time adjusts to network time smoothly; the stored clock is rewritten.
- Reboot occurs while the system clock is being updated → the ESP32 internal RTC is set atomically; no partial-write corruption is possible. The plausibility check (FR-004) guards against any stale value that survived from before the last sync.
- Device runs offline for weeks → restored time drifts with the backup clock's accuracy; time is still shown (per existing behavior of counting time internally through outages) and corrected at next sync.
- Timezone/DST: the stored time is timezone-independent; local display continues to apply the configured timezone and DST rules after restore.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The device MUST maintain the current time across reboots and power interruptions using a battery-backed clock, independent of network availability.
- **FR-002**: On boot, the device MUST restore its working time from the battery-backed clock — when that clock holds a valid time — without waiting for WiFi connection or a response from the network time source, and MUST display the restored time on screen within the normal boot period.
- **FR-003**: Network time MUST remain the authoritative source: whenever network time synchronization succeeds, the device MUST update both its working time and the battery-backed clock to the network time.
- **FR-004**: The device MUST validate the time read from the battery-backed clock before using it (at minimum, reject times earlier than the firmware build date) and MUST treat an invalid, missing, or unreadable stored time exactly like today's "time not available" state until network time is obtained.
- **FR-005**: All time-dependent functions — displayed time (local/UTC toggle), temperature log timestamps, and timezone/DST handling — MUST work identically whether the current time came from the network or was restored from the battery-backed clock.
- **FR-006**: A failure of the backup clock (absent, unresponsive, or dead battery) MUST NOT prevent the device from booting or degrade any other function; the device MUST fall back to the pre-existing network-only time behavior.
- **FR-007**: The management page's device status MUST report the current time source state: time restored from backup clock (not yet network-verified), time synchronized with network, or time not available.
- **FR-008**: The stored time MUST be kept timezone-independent so that timezone or DST configuration changes never require rewriting the backup clock.

### Key Entities

- **Backup Clock Time**: The timestamp held by the battery-backed clock; survives reboots and power loss; refreshed on every successful network synchronization; subject to validation before use.
- **Time Source State**: The device's current answer to "where did the time come from" — one of: not available, restored from backup clock, or network-synchronized. Drives display behavior and management-page status.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After a power-cycle with no network available, the correct local time (within 5 seconds of true time) is visible on screen within 15 seconds of power-on in 100% of test runs where the backup clock held a valid time.
- **SC-002**: After the device has been running offline for 7 consecutive days following a restore from the backup clock, the displayed time deviates from true time by no more than 1 minute.
- **SC-003**: Following a successful network time synchronization, a subsequent offline reboot restores a time within 5 seconds of true time — demonstrating the stored clock was refreshed by the sync.
- **SC-004**: With the backup clock absent or its battery dead, the device boots and operates all non-time features normally in 100% of test runs, and the time area shows "not available" rather than any incorrect time.
- **SC-005**: Temperature readings recorded during an offline period after a restore carry timestamps consistent with true time (within the drift bound of SC-002), with no readings stamped with a default/epoch date.

## Assumptions

- "RTC support" uses the ESP32's built-in RTC domain — no external I2C/SPI clock module is needed. The module's onboard rechargeable battery supplies the µA-level current required to keep the ESP32 RTC domain alive through main-power interruptions; when main power is restored the battery recharges automatically. If no battery is connected or the battery is fully depleted, the internal RTC loses time on power-off, triggering the graceful degradation in Story 3.
- The module's onboard battery recharges automatically whenever USB/main power is present; no low-battery warning is required in v1. Battery replacement is a rare maintenance event outside the scope of this feature.
- The backup clock stores time in a timezone-independent form; all timezone and DST logic stays where it is today (applied at display/consumption time).
- Time restored from a validated backup clock is treated as trustworthy for display and logging (no visual "unverified" warning on the main screen); the management page status (FR-007) is the place to see whether time has been network-verified since boot.
- The existing behavior of periodically re-synchronizing with the network time source continues unchanged; this feature only adds writing the result to the backup clock and reading it at boot.
- Setting the time manually (without any network) remains out of scope, as in the base specification.
