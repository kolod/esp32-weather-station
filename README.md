# ESP32 Weather Station

Always-on display of current time and temperature, built with ESP-IDF on the **Tenstar T-Display ESP32** (ST7789V 250×135 LCD, 16 MB flash, no PSRAM).

## Features

- **Display** — landscape time + temperature; left button toggles UTC/local time; right button toggles °C/°F; both settings persist across reboots
- **Temperature sensor** — Dallas DS18B20 (1-Wire), sampled every 5 s
- **WiFi onboarding** — captive portal hotspot (`weather-XXXX`) with network scan, timezone picker, and UI in English, German, French, and Ukrainian (auto-detected from browser)
- **HTTPS management page** — current readings, device status, timezone setting, OTA upload, and temperature history; backed by a private CA
- **Temperature history** — 5-minute resolution, up to 3 months retained; hourly batch writes to flash; daily purge of old data; accessible via management page and JSON API
- **OTA firmware update** — upload via management page or `curl`; automatic rollback on boot failure
- **FreeRTOS tasks** — `sensor`, `display`, and `web_server` run concurrently

## Hardware

| Part | Detail |
|---|---|
| Module | Tenstar T-Display ESP32 |
| Display | ST7789V, 250×135 px |
| Flash | 16 MB (OTA dual-partition) |
| Temperature probe | DS18B20 on GPIO27, 4.7 kΩ pull-up to 3V3 |
| Buttons | Left — UTC/local toggle · Right — °C/°F toggle |

## Requirements

- ESP-IDF **v5.4.x** (set up and exported)
- PowerShell (CA tooling in `tools/ca/`)
- OpenSSL (used by CA scripts)

## Build & Flash

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor   # adjust port as needed
```

First boot with no credentials: display shows temperature within 10 s, time shows `--:--`, and the `weather-XXXX` hotspot becomes visible.

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
| `/api/status` | GET | JSON — current temperature, time, network info, firmware version |
| `/api/history` | GET | JSON array of `{timestamp, temperature}` records (`?from=<epoch>&to=<epoch>`) |
| `/api/ota` | POST | Upload firmware binary |

Full API contract: [`specs/001-weather-station-firmware/contracts/http-api.md`](specs/001-weather-station-firmware/contracts/http-api.md)

## Project Structure

```
main/               # App entry point, FreeRTOS task launch
components/
  app_ctx/          # Shared application context (queues, handles)
  captive_dns/      # DNS redirect for captive portal
  display/          # ST7789V rendering, button handling
  history/          # Temperature log (buffer, flash codec, purge)
  sensor/           # DS18B20 driver, 1-Wire
  settings/         # NVS-backed persistent settings
  web_server/       # HTTPS server, captive portal, API handlers
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
