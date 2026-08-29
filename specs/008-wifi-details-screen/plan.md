# Implementation Plan: WiFi Details Screen

**Branch**: `008-wifi-details-screen` | **Date**: 2026-08-29 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/008-wifi-details-screen/spec.md`

## Summary

Long-pressing the right button swaps the four-quadrant weather view for a transient full-screen
overlay that shows three labelled read-only values — WiFi SSID, IPv4 address, and mDNS hostname
(`weather-XXXX.local`). The overlay auto-dismisses after 10 s, or immediately on a second right
long-press. All existing button actions are unchanged.

Technical approach: `wifi_mgr` gains one public accessor, `wifi_mgr_get_info()`, that snapshots
the active-interface SSID / IPv4 / hostname (STA values when connected, SoftAP values in
fallback). The `display` component owns the rest: `buttons.c` registers
`BUTTON_LONG_PRESS_START` on the right button and drives a one-shot `esp_timer` for the
auto-return; `ui.c` creates one opaque LVGL container in `ui_init()` (hidden) and toggles it
via `ui_show_wifi_details()` / `ui_hide_wifi_details()`, both called under `lvgl_port_lock()`.
The weather quadrants keep updating from app events underneath the overlay, so no reading is
lost (FR-010). No new fonts, no NVS setting, no web/i18n changes, no new GPIO.

## Technical Context

**Language/Version**: C (C17 / `-std=gnu23` toolchain) on ESP-IDF v6.0.2, `esp32` target.

**Primary Dependencies**: existing only — `esp_lvgl_port` / LVGL (overlay widget), `iot_button`
(`espressif/button`, already a `display` PRIV_REQUIRES) for `BUTTON_LONG_PRESS_START`,
`esp_timer` (already a `display` PRIV_REQUIRES) for the auto-return one-shot, `esp_wifi` +
`esp_netif` + `mdns` (already `wifi_mgr` dependencies) for the info snapshot. No new managed
components.

**Storage**: None. The screen is display-only; nothing is persisted (spec Assumptions).

**Testing**: `idf.py build` zero-warning gate (Constitution III); full existing regression —
`tools/test_hw_emulator.py` emulator suite and `tools/check_i18n.py` must still pass unchanged
(this feature touches neither the web server nor language packs); on-device `quickstart.md`
covering STA and AP-fallback long-press, auto-return, second-press dismiss, and
short-click / left-button non-regression. No new host unit test: the added logic is LVGL
overlay glue plus an `esp_wifi`/`esp_netif` snapshot, neither host-testable with the current
framework, and the `display` component has no existing host test target.

**Target Platform**: LilyGO T-Display-class ESP32 board, ST7789 250×135 landscape panel,
left button GPIO0 / right button GPIO35 (unchanged).

**Project Type**: Embedded firmware (existing multi-component ESP-IDF structure).

**Performance Goals**: overlay appears < 1 s after the long-press completes (SC-002); no
blocking work added to the display refresh loop, sensor task, or web server task (FR-012) —
the info snapshot is a bounded set of synchronous `esp_wifi_*` / `esp_netif_*` calls made
from the button-callback context, not from the render loop.

**Constraints**: exactly two LVGL fonts are enabled in `sdkconfig.defaults`
(`montserrat_14`, `montserrat_28`) and Principle IV forbids adding a third for this — the
overlay uses `montserrat_14` for all six lines (3 values + 3 labels), which fits within
135 px. Overlong SSIDs (up to 32 chars) are clipped by the label, not allowed to overflow
(FR-011). The overlay-visible flag is touched from the button task and the `esp_timer`
task; both mutate it only while holding `lvgl_port_lock()`, which serialises them.

**Scale/Scope**: 1 new public function in `wifi_mgr` (~40 lines), ~60 lines in `display`
(`buttons.c` long-press handler + timer, `ui.c` overlay create/show/hide, `ui.h` prototypes),
2 `CMakeLists.txt` / header doc touch-ups, 1 README line, 1 constitution hardware-note line.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. ESP-IDF Component Architecture | PASS | New cross-component read goes through a documented `wifi_mgr` public API (`wifi_mgr_get_info`), not an internal header. `display` adds `wifi_mgr` to `REQUIRES`. No new component — the screen is a natural extension of `display`, which already owns the panel and buttons. |
| II. Hardware Abstraction Layer | PASS | No new GPIO. Right-button long-press is registered inside `buttons.c` (the button driver); no raw GPIO leaves the `display` component. WiFi register/netif access stays inside `wifi_mgr`. |
| III. Build Integrity (NON-NEGOTIABLE) | PASS | Verified `idf.py build` after each change; zero warnings; no pragmas. |
| IV. Embedded Resource Discipline | PASS | No new font. Overlay is one statically-created LVGL container reused for the life of the UI (no per-press alloc). One additional `esp_timer` handle. No heap in ISR — button/timer callbacks run in task context. No flash writes. |
| V. Network & Security Standards | PASS | Read-only display of already-public LAN facts (SSID, own IP, own hostname). No secrets shown — the WiFi password is never read or rendered. No new endpoint, no captive-portal change. |

**Hardware Platform Standards**: the "Buttons" section currently documents only Left=UTC/local
and Right=°C/°F. This plan adds Right-long-press=WiFi details; `constitution.md` and `README.md`
button lines are updated to match (documentation sync, not a principle change — no version bump).

**Post-design re-check (after Phase 1)**: PASS — one accessor + one overlay widget, no
speculative abstraction (no screen-stack/navigation framework, no configurable timeout, no
QR code, no live-refresh on the overlay — all explicitly out of scope per spec Assumptions).

## Project Structure

### Documentation (this feature)

```text
specs/008-wifi-details-screen/
├── plan.md              # This file
├── research.md          # Phase 0: decisions D1–D7
├── data-model.md        # Phase 1: WiFi info snapshot struct, overlay state, screen layout
├── quickstart.md        # Phase 1: on-device validation (STA + AP fallback)
├── contracts/
│   └── wifi-details-screen.md   # Phase 1: button interaction + overlay layout + wifi_mgr_get_info contract
├── checklists/
│   └── requirements.md  # from /speckit-specify
└── tasks.md             # Phase 2 (/speckit-tasks) — not created here
```

### Source Code (repository root)

```text
components/
├── wifi_mgr/
│   ├── wifi_mgr.h        # MODIFY: add wifi_mgr_info_t + wifi_mgr_get_info() prototype/doc
│   └── wifi_mgr.c        # MODIFY: implement wifi_mgr_get_info() — snapshot SSID/IPv4/hostname
│                         #   from s_sta_netif or s_ap_netif per s_state; reuse mac_to_suffix
└── display/
    ├── CMakeLists.txt    # MODIFY: add wifi_mgr to REQUIRES
    ├── buttons.h         # MODIFY: doc comment — add "Right long-press: WiFi details screen"
    ├── buttons.c         # MODIFY: register BUTTON_LONG_PRESS_START on right → on_right_long();
    │                     #   one-shot esp_timer (10 s) → auto-hide; second press toggles/cancels
    ├── ui.h              # MODIFY: ui_show_wifi_details(const wifi_mgr_info_t*) + ui_hide_wifi_details()
    └── ui.c              # MODIFY: build hidden overlay container in ui_init(); show/hide toggles
                          #   LV_OBJ_FLAG_HIDDEN and fills the three value labels

main/                     # NO CHANGE — display_start()/wifi_mgr_start() already wired

README.md                # MODIFY: Buttons line + feature bullet — add right long-press action
.specify/memory/constitution.md   # MODIFY: Hardware Platform Standards "Buttons" line (doc sync)
```

**Structure Decision**: No new component. `display` already owns the panel, the LVGL UI, and
both buttons — the WiFi details screen is the same kind of on-panel view as the existing
quadrants, so it belongs in `ui.c` and its trigger in `buttons.c`. The only cross-component
need is *reading* current WiFi facts; that is satisfied by a single new `wifi_mgr` public
accessor (Principle I: communicate through documented APIs), with `display` taking a
`REQUIRES wifi_mgr` edge. `wifi_mgr` already depends on `esp_wifi`/`esp_netif`/`mdns`, so the
accessor adds no new dependency anywhere.

## Complexity Tracking

No constitution violations — table not required.
