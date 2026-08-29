# Phase 0 Research: WiFi Details Screen

No open `NEEDS CLARIFICATION` items — the spec resolved "AP name", "local DNS name", and
dismissal behaviour with documented assumptions. Research below records the implementation
decisions that fill the Technical Context.

## D1 — Trigger: right-button long-press event

**Decision**: Register `BUTTON_LONG_PRESS_START` on the right button handle in `buttons.c`,
mirroring the existing left-button factory-reset registration.

**Rationale**: `iot_button` already delivers `BUTTON_SINGLE_CLICK` (right = °C/°F toggle) and
`BUTTON_LONG_PRESS_START` (left = factory reset) with the default `btn_cfg` timing
(~1.5 s hold). Using `START` (not `..._UP`) fires once at the threshold, so the screen appears
while the user is still holding — matches "appears within 1 s of completing the long-press"
(SC-002) and avoids a second event on release. Single-click and long-press on the same handle
coexist: `iot_button` suppresses the click when a long-press fires.

**Alternatives considered**: `BUTTON_LONG_PRESS_UP` (fires on release — adds perceived latency
and an ambiguous "how long is long" feel); a new double-click gesture (undiscoverable, and the
spec explicitly says long-press); a dedicated Kconfig hold time (Principle IV — no speculative
config; the shared default is fine and consistent with the left button).

## D2 — Auto-return timer

**Decision**: One process-wide `esp_timer` one-shot, created once in `buttons_init()`, armed
for 10 s (`ESP_TIMER_TASK` dispatch) when the overlay is shown, `esp_timer_stop()` +
`esp_timer_start_once()` (restart) on re-show, `esp_timer_stop()` when hidden by a second
press. The timer callback calls `ui_hide_wifi_details()` under `lvgl_port_lock()`.

**Rationale**: `esp_timer` is already a `display` PRIV_REQUIRES (used by nothing in `buttons.c`
yet, but present). A one-shot avoids a polling loop in `display_task` (which sleeps 60 s — far
too coarse). The callback runs in the esp_timer task, not an ISR, so taking the LVGL lock is
legal. 10 s matches SC-003's target and is long enough to read three short lines, short enough
that a forgotten screen self-heals.

**Alternatives considered**: a FreeRTOS software timer (equivalent, but `esp_timer` is the
IDF-idiomatic choice and already linked); counting ticks in `display_task` (needs the loop
period dropped to ~1 s — wastes wakeups, Principle IV); no timeout / toggle-only (spec FR-007
requires auto-dismiss).

## D3 — Overlay rendering: hidden container toggled, not a second `lv_screen`

**Decision**: In `ui_init()`, create one opaque full-panel `lv_obj` (child of
`lv_screen_active()`), lay out three value labels + three static caption labels in a vertical
flex, and start it with `LV_OBJ_FLAG_HIDDEN`. `ui_show_wifi_details()` fills the three value
labels and clears the hidden flag; `ui_hide_wifi_details()` re-sets it. Both require the caller
to hold `lvgl_port_lock()` (same contract as every other `ui_*` function).

**Rationale**: Cheapest possible — no per-press allocation (Principle IV), no screen-load
animation machinery, no `lv_screen` swap that would also need the weather widgets re-parented.
The quadrant labels keep receiving updates from `display_event_handler` while hidden behind the
opaque overlay, so FR-010 ("updates not lost") is satisfied with zero extra code. Toggling one
flag is O(1) and safe to do from the button/timer contexts once the lock is held.

**Alternatives considered**: a distinct `lv_obj_t *screen` with `lv_screen_load()` (heavier;
must manage which screen is active on every `ui_set_*` call to avoid drawing into the hidden
one); drawing over the canvas manually (bypasses LVGL layout, fragile on the gap-offset panel);
`lv_msgbox` (styled for dialogs with buttons, wrong affordance).

## D4 — WiFi facts source: new `wifi_mgr_get_info()` accessor

**Decision**: Add `wifi_mgr_info_t { char ssid[33]; char ipv4[16]; char hostname[32]; }` and
`void wifi_mgr_get_info(wifi_mgr_info_t *out)` to `wifi_mgr.h`. Implementation branches on the
module-static `s_state`:

- **Connected (`WIFI_ST_CONNECTED`)**: `esp_wifi_sta_get_ap_info()` → `ssid`;
  `esp_netif_get_ip_info(s_sta_netif, …)` → `ipv4` via `esp_ip4addr_ntoa`.
- **AP fallback (`WIFI_ST_PROVISIONING_AP` / `WIFI_ST_AP_FALLBACK`)**:
  `esp_wifi_get_config(WIFI_IF_AP, …)` → `ssid`; `esp_netif_get_ip_info(s_ap_netif, …)` → `ipv4`
  (constant `WIFI_MGR_AP_IP_STR`, but read it live for consistency).
- **Connecting / retrying / idle**: `ssid` = configured STA SSID if present else `""`;
  `ipv4` = `""` (renders as placeholder per FR-006).
- **`hostname`** in all states: `"weather-" + mac_to_suffix() + ".local"` (reuse the existing
  helper; the string is meaningful even before mDNS starts — resolution just won't work in AP
  mode, which is acceptable per spec Assumptions).

**Rationale**: Keeps every `esp_wifi`/`esp_netif` call inside the component that owns the WiFi
domain (Principle I & II). The accessor returns copies into a caller-owned struct — no
lifetime or locking concerns for the caller. `s_state` is already the module's source of truth
(`wifi_mgr_get_state()` exists); reading it plus a few synchronous getter calls is bounded and
non-blocking (FR-012).

**Alternatives considered**: `display` calling `esp_wifi_*` directly (leaks WiFi ownership into
the display component — Principle I violation; `display` would need `esp_netif`, `mdns` added);
stashing SSID/IP into `app_ctx` on every WiFi event (more moving parts, extra mutex traffic,
data duplicated for a rarely-viewed screen); exposing three separate getters (chattier API for
no benefit — one snapshot call is atomic enough for a static display).

## D5 — Fonts and long-value handling

**Decision**: All six overlay lines use `&lv_font_montserrat_14` (the existing `FONT_LABEL`).
Value labels use `LV_LABEL_LONG_DOTS` with a fixed width of `LCD_H_RES` (250) so a 32-char
SSID clips to `…` instead of overflowing (FR-011). Captions ("Network", "IP", "Host") are
static.

**Rationale**: `sdkconfig.defaults` enables exactly `montserrat_14` and `montserrat_28`;
Principle IV forbids adding `montserrat_20`/`_16` just for this screen. Six lines × ~18 px
line height ≈ 108 px < 135 px, fits with margin. `montserrat_28` is too tall for six lines.
IPv4 (max "255.255.255.255" = 15 chars) and hostname ("weather-xxxx.local" = 18 chars) never
need clipping at 14 px on a 250 px panel; only the SSID can, hence `LONG_DOTS` on that label.

**Alternatives considered**: `montserrat_28` for values + `_14` for captions (only 3–4 lines
fit — can't show all three pairs); `LV_LABEL_LONG_SCROLL_CIRCULAR` for the SSID (motion on a
glance screen is distracting, and the spec says "no live-updating fields"); enabling a third
font (Principle IV).

## D6 — Concurrency of the overlay-visible state

**Decision**: The only mutable state is the LVGL hidden-flag on the overlay object plus the
`esp_timer` arm/disarm. All three touch points — `on_right_long()` (button task),
`auto_hide_cb()` (esp_timer task), and any future caller — perform their
show/hide/timer-restart sequence while holding `lvgl_port_lock()`. A small module-static
`bool s_wifi_overlay_shown` in `buttons.c`, read/written only under that lock, tracks state so
the second long-press knows to hide instead of re-show.

**Rationale**: `lvgl_port_lock()` already serialises `display_event_handler` against any UI
mutation; extending it to cover the overlay flag + timer calls means no new mutex. The button
and timer tasks are the only writers and they're mutually exclusive under the lock.

**Alternatives considered**: a dedicated mutex (redundant with the LVGL lock); an atomic flag
(doesn't cover the timer arm/disarm race); posting an `APP_EVENT` and handling it in
`display_task` (adds latency and needs the loop restructured — D2 rationale).

## D7 — No web / i18n / emulator changes

**Decision**: This feature is display-only. `tools/hw_emulator.py` (web-server emulator) and
the four `i18n/*.json` packs are untouched; the overlay captions are hardcoded English short
words in `ui.c` exactly like the existing "hPa" / "LOCAL" / "UTC" labels.

**Rationale**: The display UI has never been localised (all `ui.c` labels are literals) and the
emulator models HTTP endpoints, not the panel. SC-005 ("existing tests still pass") is met by
*not* touching those surfaces. Adding i18n to the panel would be a much larger, unrelated
change.

**Alternatives considered**: routing overlay strings through a new i18n mechanism (scope
explosion — the whole panel would need it for consistency); adding an emulator scenario
(nothing to emulate — no endpoint).
