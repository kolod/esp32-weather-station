# Contract: Main screen four-quadrant layout

**Feature**: 007-bme280-humidity | **Date**: 2026-08-29

Applies to the `display` component (`components/display/ui.{c,h}`, driven by `display.c`). The physical panel is an ST7789 250×135 px in landscape (`LCD_H_RES 250`, `LCD_V_RES 135` in `display.c`).

## Geometry

Origin top-left. Four quadrant containers at fixed screen positions, plus one centred glyph. Positions do **not** depend on `sensor_kind` (FR-010, FR-012).

```
 x=0            x=125          x=250
 ┌──────────────┬──────────────┐  y=0
 │  TIME        │  TEMPERATURE  │
 │  HH:MM       │   21.4        │
 │  LOCAL       │   °C          │
 ├──────────────┼──────────────┤  y=67/68   ← WiFi glyph centred on this crossing
 │  PRESSURE     │  HUMIDITY     │
 │  1013.2      │   47          │
 │  hPa         │   %           │
 └──────────────┴──────────────┘  y=135
```

Exactly **two type sizes** (user preference): one larger face for every quadrant
*value*, one smaller face for every *label*.

| Quadrant | Align / offset | Value label | Sub-label |
|----------|----------------|-------------|-----------|
| Time (TL) | `LV_ALIGN_TOP_LEFT` | `HH:MM`, Montserrat 28 | `LOCAL` / `UTC`, Montserrat 14, grey |
| Temperature (TR) | `LV_ALIGN_TOP_RIGHT` | number, Montserrat 28 | `°C` / `°F`, Montserrat 14, grey |
| Pressure (BL) | `LV_ALIGN_BOTTOM_LEFT` | number, Montserrat 28 | `hPa`, Montserrat 14, grey |
| Humidity (BR) | `LV_ALIGN_BOTTOM_RIGHT` | number, Montserrat 28 | `%`, Montserrat 14, grey |
| WiFi | `LV_ALIGN_CENTER` | `LV_SYMBOL_WIFI` (default font) | — |

`sdkconfig.defaults` enables only `CONFIG_LV_FONT_MONTSERRAT_28` (values) on top of
the default Montserrat 14 (labels); the previously bundled 20/36/48 faces are
dropped since nothing else uses them. `ui.c` references both via the
`FONT_VALUE` / `FONT_LABEL` macros. At 28 px a 6-glyph value like `1013.2`
occupies ≈85 px of the 125 px column, well clear of the centre glyph.

## Value formatting

| Reading | Valid | Invalid / unavailable |
|---------|-------|-----------------------|
| Time | `%02d:%02d` (local or UTC per mode) | `--:--` |
| Temperature | `%.1f` (°C or °F per unit) | `---` |
| Pressure | `%.1f` | `--- hPa` (value label shows `---`, sub-label stays `hPa`) |
| Humidity | `%.0f` | `--- %` (value label shows `---`, sub-label stays `%`) |

- No quadrant is ever hidden or blanked — an unavailable reading shows its placeholder (FR-012).
- WiFi glyph colour: `LV_PALETTE_LIGHT_BLUE` when `wifi_state == WIFI_ST_CONNECTED`, else `LV_PALETTE_GREY` (unchanged from today).

## `ui.h` API

```c
void ui_init(void);                                                   /* builds the 4 quadrants + glyph */
void ui_set_temperature(float value_c, bool valid, uint8_t temp_unit);/* unchanged signature */
void ui_set_pressure(float value_hpa, bool valid);                    /* `present` param REMOVED */
void ui_set_humidity(float value_pct, bool valid);                    /* NEW */
void ui_set_time(bool synced, time_t now, uint8_t time_mode);         /* unchanged */
void ui_set_wifi_state(int wifi_state);                               /* unchanged */
```

All setters must be called inside `lvgl_port_lock()` (unchanged rule).

### `display.c` call-site changes

- Read `app_state.humidity` alongside `reading` / `pressure` under `app_state_mutex`.
- `ui_set_pressure(p.value_hpa, p.valid)` — drop the `sensor == SENSOR_BMP280` argument.
- `ui_set_humidity(h.value_pct, h.valid)` — added.
- No new event subscription; the existing `APP_EVT_READING_UPDATED` / time / wifi / settings handlers already cover it.

## Test obligations

- LCD rendering is validated on-device via [quickstart.md](../quickstart.md), not unit-tested.
- The quickstart MUST confirm: (a) the four corners hold time/temp/pressure/humidity in the specified positions on a BME280 station; (b) a BMP280 station shows `--- %` in the humidity quadrant with everything else populated; (c) a probe-only station shows `--- hPa` and `--- %` placeholders; (d) the WiFi glyph is centred and tracks connection state; (e) °C/°F and LOCAL/UTC toggles update only their own quadrant.
