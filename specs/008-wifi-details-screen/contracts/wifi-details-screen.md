# Contract: WiFi Details Screen

Three contracts: the `wifi_mgr` public accessor, the button interaction, and the `ui` overlay API.

---

## 1. `wifi_mgr` public API (new)

```c
/* wifi_mgr.h */

typedef struct {
    char ssid[33];      /* joined SSID (STA) or broadcast SSID (AP fallback); "" if unknown  */
    char ipv4[16];      /* dotted-quad IPv4 of the active interface; "" if no IP yet          */
    char hostname[32];  /* "weather-XXXX.local" — always populated                            */
} wifi_mgr_info_t;

/**
 * @brief Snapshot the current WiFi-reachability facts into *out.
 *
 * Non-blocking, safe from any task and at any time (before or after wifi_mgr_start()).
 * Chooses STA-interface values when connected, SoftAP-interface values in AP fallback,
 * and stored/empty values while connecting. All fields NUL-terminated on return.
 *
 * @param out  caller-owned struct; fully overwritten (zeroed first).
 */
void wifi_mgr_get_info(wifi_mgr_info_t *out);
```

### Behaviour table

| Precondition | `ssid` | `ipv4` | `hostname` |
|--------------|--------|--------|------------|
| `wifi_mgr_get_state() == WIFI_ST_CONNECTED` | current AP's SSID | STA IPv4 (e.g. `192.168.1.42`) | `weather-a4f2.local` |
| state is `PROVISIONING_AP` or `AP_FALLBACK` | `weather-a4f2` | `192.168.16.1` | `weather-a4f2.local` |
| state is `CONNECTING` / `RETRYING` | stored STA SSID or `""` | `""` | `weather-a4f2.local` |
| state is `IDLE` (pre-start) | `""` | `""` | `weather-a4f2.local` |

### Guarantees

- No call blocks; no call posts events; no call takes `app_state_mutex` for longer than a
  field copy.
- Idempotent — repeated calls with unchanged link state return identical bytes.
- `out` is never partially written: on any internal getter failure the affected field is left
  as `""` (still valid, renders as placeholder).

---

## 2. Button interaction contract (`display/buttons.c`)

| Input | Precondition | Effect |
|-------|-------------|--------|
| Right button `BUTTON_LONG_PRESS_START` | overlay hidden | snapshot via `wifi_mgr_get_info()`; `lvgl_port_lock()`; `ui_show_wifi_details(&info)`; `s_wifi_overlay_shown = true`; start/restart `s_auto_hide_timer` (10 s); `lvgl_port_unlock()` |
| Right button `BUTTON_LONG_PRESS_START` | overlay shown | `lvgl_port_lock()`; `ui_hide_wifi_details()`; `s_wifi_overlay_shown = false`; `esp_timer_stop(s_auto_hide_timer)`; `lvgl_port_unlock()` |
| `s_auto_hide_timer` fires | overlay shown | `lvgl_port_lock()`; `ui_hide_wifi_details()`; `s_wifi_overlay_shown = false`; `lvgl_port_unlock()` |
| Right button `BUTTON_SINGLE_CLICK` | any | unchanged — toggles `TEMP_UNIT` (existing `on_right_click`) |
| Left button `BUTTON_SINGLE_CLICK` | any | unchanged — toggles `TIME_MODE` |
| Left button `BUTTON_LONG_PRESS_START` | any | unchanged — factory reset |

### Guarantees

- Long-press callback returns quickly (snapshot + one flag toggle + one timer call); no
  network wait, no flash write.
- A long-press while `SINGLE_CLICK` would also match is resolved by `iot_button` in favour of
  the long-press (click suppressed) — the °C/°F toggle does **not** also fire.
- Timer restart on re-show: `esp_timer_stop()` then `esp_timer_start_once()` — never two
  concurrent arms.
- All overlay-state mutation happens under `lvgl_port_lock()`; the button task and esp_timer
  task cannot interleave a show with a hide.

---

## 3. `ui` overlay API (`display/ui.h`)

```c
/** Show the WiFi details overlay, filling the three value labels from *info.
 *  Caller MUST hold lvgl_port_lock(). Idempotent (re-show just refreshes text). */
void ui_show_wifi_details(const wifi_mgr_info_t *info);

/** Hide the WiFi details overlay, revealing the weather quadrants.
 *  Caller MUST hold lvgl_port_lock(). No-op if already hidden. */
void ui_hide_wifi_details(void);
```

### Guarantees

- `ui_show_wifi_details(NULL)` is a no-op guard (defensive; never called with NULL in practice).
- Overlay object is created once in `ui_init()` and only ever toggled via
  `LV_OBJ_FLAG_HIDDEN` — no allocation per show (Constitution IV).
- Weather quadrant labels keep updating while the overlay is shown; `ui_set_temperature` /
  `ui_set_pressure` / `ui_set_humidity` / `ui_set_time` / `ui_set_wifi_state` are unaffected
  and do not need overlay awareness.
- Value labels use `LV_LABEL_LONG_DOTS` at width `LCD_H_RES`; an SSID longer than the panel
  width is clipped with `…`, never overflows (FR-011).

---

## Verification (no automated contract test — display is not emulated)

| Contract | How verified |
|----------|--------------|
| `wifi_mgr_get_info` behaviour table | on-device: quickstart steps in STA and AP-fallback modes; serial `ESP_LOGI` of the snapshot on each long-press during bring-up |
| Button interaction | on-device quickstart: long-press shows; second long-press hides; 10 s auto-hide; right click still toggles unit; left button unaffected |
| `ui` overlay API | on-device visual check; `idf.py build` zero-warning gate |
| Non-regression | `tools/test_hw_emulator.py` + `tools/check_i18n.py` unchanged and green |
