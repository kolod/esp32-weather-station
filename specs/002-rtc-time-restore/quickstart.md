# Quickstart Validation: RTC Time Restore After Reboot

**Feature**: 002-rtc-time-restore | Validates SC-001…SC-005 from [spec.md](./spec.md)

## Prerequisites

- Tenstar T-Display flashed with this feature's firmware (`idf.py build flash monitor`)
- A rechargeable Li-Po battery connected to the module's battery connector (charged ≥ a few minutes on USB)
- A WiFi network whose internet access (or at least UDP/123) you can cut on demand — a phone hotspot works
- Device already onboarded (feature 001 captive portal) and management page reachable
- `idf.py monitor` attached where log lines are cited

## Unit tests (no hardware state needed)

```powershell
# From repo root — runs Unity tests of the rtc_time component (record CRC/magic,
# plausibility bounds, state transitions) on target or host runner per 001 conventions
idf.py build   # CI gate: feature must not break the build
```

Expected: build succeeds; `components/rtc_time/test` cases all pass.

## Scenario A — Restore after offline power-cycle (SC-001, SC-003, User Story 1)

1. Power the device with network available; wait for `SNTP synchronized` in the log and correct time on screen (`/api/status` → `"time_source":"ntp"`).
2. Disable the WiFi network entirely (power off AP / hotspot).
3. Unplug USB, wait ~10 s (board runs from battery), replug USB — then, for a stricter variant, instead press the reset button.
4. **Expected**: within 15 s of boot the screen shows the correct local time (within 5 s of a reference clock) with no network present. Log shows `time restored from RTC`. `/api/status` (once AP-mode or LAN reachable) shows `"time_source":"rtc"` and a non-null `time_last_sync`.
5. Repeat 5×. **Pass**: 5/5 runs show correct time (SC-001); restored offset ≤ 5 s (SC-003).

## Scenario B — NTP unreachable but WiFi present (User Story 1, scenario 2)

1. From the synced state, block internet/NTP on the AP (keep WiFi up).
2. Reset the device.
3. **Expected**: correct time on screen within the boot period; `"time_source":"rtc"`; no wait for SNTP; when NTP is later unblocked, next sync flips status to `"ntp"` without a visible time jump (edge case: smooth adjustment).

## Scenario C — Sync refreshes the backup (SC-003, User Story 2)

1. With the device online, note `time_last_sync` from `/api/status`.
2. Wait for the next periodic SNTP poll (or toggle WiFi to force one); confirm `time_last_sync` advanced.
3. Go offline, reset, and confirm restored time is within 5 s of true time.

## Scenario D — Dead/absent battery degrades gracefully (SC-004, User Story 3)

1. Disconnect the battery. Unplug USB for ≥ 30 s (RTC domain fully discharges), then power via USB with no network.
2. **Expected**: device boots normally; temperature display works; time area shows "not available" (never a wrong time); log shows restore rejection reason; `/api/status` shows `"time_source":"none"`, `"time_last_sync":null`. All non-time features usable (SC-004).
3. Reconnect network → time appears after SNTP sync; `"time_source":"ntp"`.

## Scenario E — Offline drift over 7 days (SC-002, run in background)

1. From a synced state, disconnect the network and leave the device powered for 7 days.
2. Compare displayed time against a reference clock.
3. **Pass**: deviation ≤ 1 minute. (Worst-case crystal budget ≈ 24 s — research.md R6.)

## Scenario F — History timestamps during offline restore (SC-005)

1. After Scenario A step 4 (running on RTC-restored time, offline), let the device record ≥ 1 h of readings.
2. Reconnect the network, open the management page history (or `GET` the history JSON endpoint — 001 contract).
3. **Pass**: readings from the offline window carry timestamps consistent with true time (within SC-002 drift bound); no epoch-1970 or missing timestamps.

## Troubleshooting

- Restore always fails after power-cycle → verify the battery is actually charged/connected and that `CONFIG_ESP_SYSTEM_TIME_SOURCE` wasn't overridden (build fails by contract if so — see contracts/rtc-component-api.md).
- Restore fails only after reflashing → expected once: flashing usually power-cycles the RTC domain only if power was removed; a plain `idf.py flash` keeps it, but the very first boot of a device that has never synced must show "not available" (Story 3, scenario 1).
