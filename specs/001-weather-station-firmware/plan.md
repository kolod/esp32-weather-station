# Implementation Plan: ESP32 Weather Station Firmware

**Branch**: `001-weather-station-firmware` | **Date**: 2026-07-11 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/001-weather-station-firmware/spec.md`

## Summary

Firmware for a Tenstar T-Display ESP32 module (16 MB flash, no PSRAM) that shows current time and DS18B20 temperature on a landscape ST7789V TFT, onboards to WiFi via an open `weather-XXXX` hotspot with a localized captive portal (en/de/fr/uk), serves an HTTPS management page authenticated by certificates from an owner-operated private CA, records a 3-month temperature history to flash, and supports OTA updates with automatic rollback. Built on ESP-IDF 5.x with FreeRTOS tasks `web_server`, `sensor`, and `display`; project scaffolded with `idf.py create-project`.

## Technical Context

**Language/Version**: C (C17), ESP-IDF v5.4.x (latest stable 5.x), CMake build via `idf.py`

**Primary Dependencies**:
- `espressif/ds18b20` ^0.4.0 (+ transitive `onewire_bus`, RMT-based 1-Wire) — temperature probe
- `espressif/esp_lvgl_port` ^2.8.0 with LVGL 9 — display stack over built-in `esp_lcd` (`esp_lcd_new_panel_st7789`)
- `espressif/button` (iot_button) — debounced GPIO buttons
- `espressif/mdns` — `weather-XXXX.local` hostname (stable name for device certificates)
- `joltwallet/littlefs` — flash filesystem for history log + device certificate/key
- ESP-IDF built-ins: `esp_wifi`, `esp_netif`, `esp_http_server`, `esp_https_server`, `esp_ota_ops` + app rollback, `esp_netif_sntp`, `nvs_flash`, LEDC (backlight)

**Storage**:
- NVS: WiFi credentials (native `esp_wifi` NVS storage), timezone, time-display mode (local/UTC), temperature unit (°C/°F)
- LittleFS partition: device TLS certificate + private key (`/storage/certs/`), temperature history as monthly binary log files (`/storage/history/`)
- Partition table (16 MB): `nvs` 64 KB, `otadata` 8 KB, `phy_init` 4 KB, `ota_0` 3 MB, `ota_1` 3 MB, `storage` (littlefs) ≈ 9.8 MB

**Testing**: Unity component tests for pure logic (history ring/record codec, Accept-Language parser, timezone mapping) + `idf.py build` CI gate + manual validation per quickstart.md

**Target Platform**: ESP32 (Xtensa dual-core, 520 KB SRAM, no PSRAM), Tenstar T-Display board, 16 MB flash

**Project Type**: Embedded firmware (single ESP-IDF project, repo root = project root)

**Performance Goals**: Temperature on screen <10 s after boot; display never freezes on network stalls; SNTP-synced clock within 2 s; OTA <5 min; 7-day continuous operation

**Constraints**: No PSRAM → LVGL partial buffers (2× ~16 KB DMA, not full-frame); HTTPS TLS sessions limited (≈2 concurrent) to bound heap; hourly single-write history persistence to limit flash wear; all settings writes power-loss atomic (NVS guarantees; littlefs copy-on-write)

**Scale/Scope**: Single device class, 1 screen, ~7 firmware components, 4 portal languages, ≈26 000 history records (≈210 KB) over 3 months

**Fixed hardware map (authoritative, from user input)**:

| Function | GPIO | Notes |
|---|---|---|
| Display MOSI | 19 | SPI2 host |
| Display SCLK | 18 | |
| Display CS | 5 | |
| Display DC | 16 | |
| Display RESET | 23 | |
| Display backlight | 4 | LEDC PWM |
| Key left | 0 | Active-low, internal pull-up (boot strap pin) |
| Key right | 35 | Input-only, external pull-up on board |
| DS18B20 data | 27 | RMT 1-Wire, external 4.7 kΩ pull-up |

Display panel: ST7789V, 250×135 landscape per user input (see research.md R2 — verify panel offsets on hardware; common T-Display panels are 240×135 with x/y gap offsets).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

`.specify/memory/constitution.md` is the unmodified template — no project principles have been ratified. **No gates to enforce; check passes vacuously.** General engineering defaults applied instead: single project, no speculative abstraction, component-per-concern with clear public headers.

*Post-Phase-1 re-check*: design introduces no additional projects or patterns beyond ESP-IDF component conventions. Still passes.

## Project Structure

### Documentation (this feature)

```text
specs/001-weather-station-firmware/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   ├── http-api.md      # Portal + management REST/HTML contract
│   └── storage-schema.md# NVS keys, littlefs layout, history record format
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created by this command)
```

### Source Code (repository root)

Scaffolded with `idf.py create-project esp32-weather-station` (executed in repo root, project files at root), then extended:

```text
CMakeLists.txt                 # top-level ESP-IDF project file
partitions.csv                 # custom 16 MB partition table
sdkconfig.defaults             # target, flash size, rollback, TLS, LVGL config
main/
├── CMakeLists.txt
├── idf_component.yml          # managed component dependencies
├── main.c                     # NVS init, event loop, component start-up, task creation
└── app_ctx.h                  # shared app context/event definitions
components/
├── settings/                  # NVS-backed settings (tz, unit, time mode) + change events
├── sensor/                    # sensor task: ds18b20 read @5 s, publishes reading + validity
├── history/                   # 5-min sampling ring, hourly littlefs append, daily purge, query API
├── display/                   # display task: esp_lcd ST7789 + esp_lvgl_port UI, buttons
├── wifi_mgr/                  # STA connect/retry, AP fallback (weather-XXXX), mDNS, SNTP start
├── captive_dns/               # DNS hijack server for captive portal (AP mode)
└── web_server/                # web_server task: HTTP portal (AP) + HTTPS mgmt (STA), OTA endpoint
    └── www/                   # embedded, gzipped portal + management page assets (i18n strings)
tools/
└── ca/                        # OpenSSL scripts: create CA, issue/install device certs, provision
tests/
└── (component-level Unity tests live in components/*/test/)
```

**Structure Decision**: Single ESP-IDF project at repo root (matches `idf.py create-project` output). Each concern is an IDF component with one public header; `main` only wires components together and starts the three required FreeRTOS tasks (`web_server`, `sensor`, `display`). Web assets are embedded into the app image (gzipped) rather than served from the storage partition, so a firmware OTA atomically updates UI + code together.

## Complexity Tracking

No constitution violations to justify (constitution not ratified). No extra projects or speculative patterns introduced.
