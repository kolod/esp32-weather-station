# Phase 1 Data Model: WiFi Details Screen

This feature adds no persistent data. The "model" is one transient snapshot struct and the
in-memory overlay state.

## Entity: `wifi_mgr_info_t` (new, `wifi_mgr.h`)

A caller-owned value snapshot of the current WiFi-reachability facts. Filled by
`wifi_mgr_get_info()`, consumed immediately by the display, never stored.

| Field | Type | Meaning | Empty / placeholder case |
|-------|------|---------|--------------------------|
| `ssid` | `char[33]` | SSID of the joined network (STA connected) or the broadcast SoftAP SSID (`weather-XXXX`, AP fallback). NUL-terminated; 32 chars max per 802.11. | `""` when connecting with no prior AP info and no stored SSID. Rendered as `---`. |
| `ipv4` | `char[16]` | Dotted-quad IPv4 of the active interface (`s_sta_netif` when connected, `s_ap_netif` in fallback). | `""` when no interface has an IP yet (connecting / retrying / idle). Rendered as `---`. |
| `hostname` | `char[32]` | mDNS name in browser-typable form: `weather-<mac-suffix>.local`. Always populated (derived from MAC, independent of link state). | never empty |

### Population rules (by `s_state`, evaluated inside `wifi_mgr_get_info`)

| `s_state` | `ssid` source | `ipv4` source |
|-----------|---------------|---------------|
| `WIFI_ST_CONNECTED` | `esp_wifi_sta_get_ap_info()->ssid` | `esp_netif_get_ip_info(s_sta_netif)` |
| `WIFI_ST_PROVISIONING_AP`, `WIFI_ST_AP_FALLBACK` | `esp_wifi_get_config(WIFI_IF_AP)->ap.ssid` | `esp_netif_get_ip_info(s_ap_netif)` (= `WIFI_MGR_AP_IP_STR`) |
| `WIFI_ST_CONNECTING`, `WIFI_ST_RETRYING` | stored STA SSID (`esp_wifi_get_config(WIFI_IF_STA)`) if non-empty, else `""` | `""` |
| `WIFI_ST_IDLE` | `""` | `""` |

`hostname` is always `snprintf("weather-%s.local", mac_to_suffix())`.

### Validation / invariants

- All three buffers are always NUL-terminated on return (fields zero-initialised, getters
  bounded by buffer size).
- `wifi_mgr_get_info()` performs only synchronous, non-blocking IDF getter calls — no waiting
  on events, no network I/O (FR-012).
- Safe to call from any task at any time, including before `wifi_mgr_start()` completes
  (returns the `WIFI_ST_IDLE` shape).

## Entity: WiFi overlay (in-memory, `display` component)

| State | Owner | Values | Transitions |
|-------|-------|--------|-------------|
| `s_wifi_overlay_shown` | `buttons.c` static `bool`, accessed only under `lvgl_port_lock()` | `false` (weather view) / `true` (overlay visible) | `false→true` on right `LONG_PRESS_START`; `true→false` on next right `LONG_PRESS_START` or on `auto_hide` timer fire |
| overlay widget | `ui.c` static `lv_obj_t *s_wifi_overlay` (+ 3 value-label handles) | created once in `ui_init()`, `LV_OBJ_FLAG_HIDDEN` set/cleared | flag cleared by `ui_show_wifi_details()`, set by `ui_hide_wifi_details()` |
| `s_auto_hide_timer` | `buttons.c` static `esp_timer_handle_t` | one-shot, 10 000 000 µs | started/restarted on show; stopped on hide |

### State machine

```text
        right LONG_PRESS_START                 right LONG_PRESS_START
WEATHER ───────────────────────▶ WIFI_DETAILS ───────────────────────▶ WEATHER
   ▲                                  │
   └──────────────────────────────────┘
        auto_hide timer (10 s)
```

- Sensor / time / WiFi-state app events during `WIFI_DETAILS`: handled normally by
  `display_event_handler` (writes the hidden quadrant labels); visible again on return to
  `WEATHER` (FR-010).
- Right `SINGLE_CLICK` during `WIFI_DETAILS`: still toggles °C/°F (setting changes, no visible
  effect on the overlay — it shows no temperature).
- Left click / left long-press during `WIFI_DETAILS`: unchanged (time mode / factory reset).

## Screen layout (250 × 135, overlay)

```text
┌────────────────────────────────────────────┐
│  Network                                   │  caption  (montserrat_14, grey)
│  MyHomeWiFi                                 │  value    (montserrat_14, white, LONG_DOTS @250)
│  IP                                         │
│  192.168.1.42                               │
│  Host                                       │
│  weather-a4f2.local                         │
└────────────────────────────────────────────┘
   opaque black bg, vertical flex, left-aligned, ~4 px row gap
```

- Background: solid black (`lv_color_black()`), fully opaque, covers the whole panel so
  quadrant text never bleeds through.
- No border, no title bar, no buttons, no scrollbar (`LV_OBJ_FLAG_SCROLLABLE` removed).
- Captions are fixed literals in `ui.c` (`"Network"`, `"IP"`, `"Host"`) — not localised,
  consistent with existing panel labels (`"hPa"`, `"LOCAL"`).

## i18n / storage impact

None. No NVS keys, no `i18n/*.json` keys, no files, no HTTP payload.
