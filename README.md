# ESP32 Weather Station

Always-on display of current time, temperature, pressure and humidity, built with ESP-IDF on the **Tenstar T-Display ESP32** (ST7789V 250×135 LCD, 16 MB flash, no PSRAM).

## Features

- **Display** — fixed four-quadrant layout: time (top-left), temperature (top-right), pressure (bottom-left), humidity (bottom-right), with the WiFi status glyph in the centre. Left button toggles UTC/local time; right button toggles °C/°F; both settings persist across reboots. A right long-press shows a WiFi details screen (network name, IP address, `weather-XXXX.local` hostname) that auto-returns after 10 s. Quadrants with no reading available show a dashed placeholder.
- **Environmental sensor** — autodetected at boot: **BME280** (temperature + pressure + humidity) → **BMP280** (temperature + pressure) → **DS18B20** 1-Wire probe (temperature only) → none. Sampled every 5 s; no configuration needed, a hardware swap is plug-and-reboot.
- **WiFi onboarding** — captive portal hotspot (`weather-XXXX`) with network scan, timezone picker, and UI in English, German, French, and Ukrainian (auto-detected from browser)
- **HTTPS management page** — current readings in a live-updating 2×2 grid (time, temperature, pressure, humidity), device status, timezone setting, OTA upload, an inline boot log, and a measurement-history plot with Day / Week / Month / All ranges; backed by a private CA. Readings stream over a WebSocket (`wss://`) and refresh within ~1 s of each sample, with automatic reconnect and a polling fallback.
- **Measurement history** — temperature, plus pressure and humidity when the fitted sensor supplies them; 5-minute resolution, up to 3 months retained; hourly batch writes to flash; daily purge of old data; viewable as a plot on the management page and exportable as JSON or CSV
- **OTA firmware update** — upload via management page or `curl`; automatic rollback on boot failure
- **FreeRTOS tasks** — `sensor`, `display`, and `web_server` run concurrently

## Hardware

| Part | Detail |
|---|---|
| Module | Tenstar T-Display ESP32 |
| Display | ST7789V, 250×135 px |
| Flash | 16 MB (OTA dual-partition) |
| Environmental sensor | BME280 or BMP280 on I2C (SCL GPIO22, SDA GPIO21, addr 0x76/0x77) |
| Temperature probe (fallback) | DS18B20 on GPIO27, 4.7 kΩ pull-up to 3V3 |
| Buttons | Left — UTC/local toggle (long-press: factory reset) · Right — °C/°F toggle (long-press: WiFi details screen) |

## Requirements

- ESP-IDF **v6.0.2** (set up and exported)
- PowerShell (CA tooling in `tools/ca/`)
- OpenSSL (used by CA scripts)

## Build & Flash

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor   # adjust port as needed
```

First boot with no credentials: the display populates its quadrants within 10 s (readings the fitted sensor cannot provide show `---`), time shows `--:--`, and the `weather-XXXX` hotspot becomes visible.

## First-Time Setup

1. Connect a phone to the `weather-XXXX` hotspot.
2. The captive portal opens automatically — enter your network name, password, and timezone.
3. The device joins your network and the hotspot disappears.

## HTTPS & Private CA

```powershell
# Create the CA once
./tools/ca/ca-create.ps1 -Org "Home"

# Issue and flash a certificate for each device (suffix = last 2 MAC bytes, e.g. a1b2)
./tools/ca/device-issue.ps1 -Suffix a1b2
./tools/ca/device-provision.ps1 -Port COM5
```

Install `ca.crt` into your OS/browser trust store once; all devices signed by that CA will be trusted without warnings.

## API

The management page is reachable at `https://weather-XXXX.local`. Key endpoints:

| Endpoint | Method | Description |
|---|---|---|
| `/api/status` | GET | JSON — current readings, time, network info, firmware version |
| `/api/ws` | GET | WebSocket (`wss://`) — pushes the `/api/status` snapshot to the page on every new reading; the page falls back to polling `/api/status` when the socket is unavailable |
| `/api/history` | GET | JSON array of `{timestamp, temperature, pressure, humidity}` records (`?from=<epoch>&to=<epoch>`) |
| `/api/history.csv` | GET | Full history as CSV (`timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct`) |
| `/api/boot.log` | GET | Plain-text boot diagnostics captured during startup |
| `/api/ota` | POST | Upload firmware binary |

Full API contract: [`specs/001-weather-station-firmware/contracts/http-api.md`](specs/001-weather-station-firmware/contracts/http-api.md)

## Project Structure

```
main/               # App entry point, FreeRTOS task launch
components/
  app_ctx/          # Shared application context (queues, handles)
  captive_dns/      # DNS redirect for captive portal
  display/          # ST7789V rendering, button handling
  history/          # Measurement log (buffer, flash codec, purge)
  sensor/           # BME280/BMP280 (I2C) + DS18B20 (1-Wire) drivers, boot-time autodetect
  settings/         # NVS-backed persistent settings
  web_server/       # HTTPS server, captive portal, REST + WebSocket API handlers
  wifi_mgr/         # STA connect, AP fallback, reconnect logic
tools/
  ca/               # CA creation and device certificate scripts
specs/
  001-weather-station-firmware/
    spec.md         # Feature specification
    plan.md         # Implementation design
    tasks.md        # Task list
    quickstart.md   # Validation procedures
    contracts/      # HTTP API and storage schema
```

## Validation

See [`specs/001-weather-station-firmware/quickstart.md`](specs/001-weather-station-firmware/quickstart.md) for end-to-end validation procedures covering all user stories and success criteria.

## License

See [LICENSE](LICENSE).
