# Contract: Readings & History API changes (humidity)

**Feature**: 007-bme280-humidity | **Date**: 2026-08-29

Applies to the firmware management server (HTTPS :443) and the hardware emulator's management endpoint, which MUST stay in lockstep. All changes are **additive** — every field from feature 005's `readings-api.md` keeps its name, type, position and semantics. This document records only the deltas.

## 1. `GET /api/status` — new fields + widened value

```json
{
  "temperature_c": 21.37,
  "temperature_valid": true,
  "pressure_hpa": 1013.2,
  "pressure_valid": true,
  "humidity_pct": 47.5,
  "humidity_valid": true,
  "sensor": "bme280",
  "...": "all existing fields unchanged"
}
```

| Field | Type | Semantics |
|-------|------|-----------|
| `humidity_pct` | number | Relative humidity in %RH, 0.1 resolution. Last valid value; meaningful only when `humidity_valid` is true. |
| `humidity_valid` | boolean | `true` only when `sensor == "bme280"` and the current reading is in 0–100 %RH. `false` for every other fitting or on read error / out of range. |
| `sensor` | string | Now one of `"bme280"`, `"bmp280"`, `"ds18b20"`, `"none"`. Fixed until reboot. |
| `pressure_valid` | boolean | **Widened**: now `true` for `sensor == "bmp280"` **or** `"bme280"` (was `"bmp280"` only). |

Rules:
- `humidity_pct` / `humidity_valid` are ALWAYS present regardless of fitting — no key-existence probing.
- With `humidity_valid:false`, clients MUST NOT display `humidity_pct` (render the unavailable state).
- `temperature_valid` / `temperature_c` semantics unchanged; in `"bme280"` mode they reflect the BME280's temperature.

## 2. `GET /api/history` — record shape

```json
{ "records": [
  { "timestamp": 1783939200, "temperature": 21.37, "pressure": 1013.2, "humidity": 47.5 },
  { "timestamp": 1783939500, "temperature": 21.35, "pressure": 1013.1, "humidity": null }
] }
```

- `humidity` is ALWAYS present per record: a number (%RH) when the sample recorded humidity, JSON `null` otherwise (records written before this feature, non-BME280 operation, or invalid humidity at sample time).
- `null` and `0` are never conflated; `0` is not a legal recorded value (FR-006 / FR-007).
- `pressure` semantics unchanged. Record order and `from`/`to` filtering unchanged.

## 3. `GET /api/history.csv` — column addition

```csv
timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct
2026-08-29T10:00:00Z,21.37,1013.2,47.5
2026-08-29T10:05:00Z,21.35,1013.1,
```

- Header gains a 4th column `humidity_pct`; the first three columns keep position and format.
- Humidity cell is empty (not `0`, not `null`) when the sample has no humidity.

## 4. Management page rendering (UI contract)

- Current Readings shows a **humidity row** (localized label `mgmt_label_humidity`, fixed `%` unit) when `sensor == "bme280"`; the same unavailable treatment as temperature (`---`) when the BME280 reading is momentarily invalid; the row is hidden entirely when `sensor` is not `"bme280"`.
- The pressure row is now shown for `sensor` `"bmp280"` **or** `"bme280"`.
- History table adds a humidity column only when the loaded data contains at least one non-null humidity (same rule as the pressure column).
- All new labels localized in en/de/fr/uk via the feature-004 mechanism; inventory enforced by `tools/check_i18n.py`.

## 5. Emulator scenario extension

`PUT /emu/scenario` — the `sensor` field gains a value:

| Field | Values | Default | Effect |
|-------|--------|---------|--------|
| `sensor` | `"bme280"` \| `"bmp280"` \| `"ds18b20"` \| `"none"` | `"bmp280"` | `"bme280"` populates `humidity_*` and (like `"bmp280"`) `pressure_*` in status; new history samples carry humidity while active. Other values leave humidity absent. |

`sensor_valid` keeps its meaning and composes with `sensor` (an invalid BME280 → `humidity_valid:false`, `pressure_valid:false`, `temperature_valid:false`).

## Contract test obligations

1. Status shape per fitting (`bme280` / `bmp280` / `ds18b20` / `none`): `humidity_pct` + `humidity_valid` always present with correct values; `pressure_valid` true for both Bosch fittings.
2. History JSON: numeric humidity and `null` both occur and parse across a mid-stream fitting switch; no `0`-for-absent.
3. CSV: 4-column header; empty cell for absent humidity; first three columns byte-identical in format to feature 005.
4. Non-BME280 regression: with `sensor` `"bmp280"` / `"ds18b20"` / `"none"`, no humidity *value* appears in any rendered UI surface or history payload (SC-003).
