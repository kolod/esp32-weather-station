# Contract: Readings & History API changes (pressure)

**Feature**: 005-bmp280-sensor | **Date**: 2026-07-14

Applies to the firmware management server (HTTPS :443) and the hardware emulator's management endpoint, which MUST stay in lockstep. All changes are **additive** — existing fields keep their names, types, and semantics. Supersedes nothing; extends the shapes documented in `specs/003-hardware-emulator/contracts/mgmt-api.md`.

## 1. `GET /api/status` — new fields

```json
{
  "temperature_c": 21.37,
  "temperature_valid": true,
  "pressure_hpa": 1013.2,
  "pressure_valid": true,
  "sensor": "bmp280",
  "...": "all existing fields unchanged"
}
```

| Field | Type | Semantics |
|-------|------|-----------|
| `pressure_hpa` | number | Station pressure in hPa, 0.1 resolution. Last valid value; meaningful only when `pressure_valid` is true. |
| `pressure_valid` | boolean | `false` when no BMP280 is fitted, or the fitted BMP280 currently returns no valid reading. |
| `sensor` | string | Boot-time detection result: `"bmp280"`, `"ds18b20"`, or `"none"`. Fixed until reboot. |

Rules:
- The three fields are ALWAYS present regardless of fitting (clients need no key-existence probing; SC-003 checks probe-only stations report `"sensor":"ds18b20"`, `"pressure_valid":false`).
- With `pressure_valid:false`, clients MUST NOT display `pressure_hpa` (render the unavailable state, as with temperature).
- `temperature_valid` semantics are unchanged; in `"bmp280"` mode it reflects the BMP280's temperature reading.

## 2. `GET /api/history` — record shape

```json
{ "records": [
  { "timestamp": 1783939200, "temperature": 21.37, "pressure": 1013.2 },
  { "timestamp": 1783939500, "temperature": 21.35, "pressure": null }
] }
```

- `pressure` is ALWAYS present per record: a number (hPa) when the sample recorded pressure, JSON `null` otherwise (pre-upgrade v1 records, probe-only operation, or invalid pressure at sample time).
- `null` and `0` are never conflated; `0` is not a legal recorded value (FR-006/FR-007).
- Record order, filtering (`from`/`to`), and all other behavior unchanged.

## 3. `GET /api/history.csv` — column addition

```csv
timestamp_iso8601,temperature_c,pressure_hpa
2026-07-14T10:00:00Z,21.37,1013.2
2026-07-14T10:05:00Z,21.35,
```

- Header gains third column `pressure_hpa`; existing two columns keep position and format.
- Pressure cell is empty (not `0`, not `null`) when the sample has no pressure.

## 4. Management page rendering (UI contract)

- Current Readings shows a pressure row (localized label, fixed "hPa" unit) when `pressure_valid` is true; shows the same unavailable treatment as temperature (`---`) when `sensor` is `"bmp280"` but the reading is currently invalid; the row is hidden entirely when `sensor` is not `"bmp280"`.
- History table adds a pressure column only when the loaded data contains at least one non-null pressure.
- All new labels localized in en/de/fr/uk via the feature-004 mechanism (`data-i18n`, `t()`), inventory enforced by `tools/check_i18n.py`.

## 5. Emulator scenario extension

`PUT /emu/scenario` accepts a new field:

| Field | Values | Default | Effect |
|-------|--------|---------|--------|
| `sensor` | `"bmp280"` \| `"ds18b20"` \| `"none"` | `"bmp280"` | Drives `sensor`/`pressure_*` in status; whether new history samples carry pressure; `"none"` also forces `temperature_valid:false`. |

Existing scenario field `sensor_valid` keeps its meaning (validity of the active sensor's readings) and composes with `sensor`.

## Contract test obligations

1. Status shape per fitting (`bmp280` / `ds18b20` / `none`): the three new fields present with correct values in every mode.
2. History JSON: numeric pressure and `null` both occur and parse; no `0`-for-absent.
3. CSV: three-column header; empty cell for absent pressure; existing columns byte-identical in format.
4. Probe-only regression: with `sensor:"ds18b20"`, no pressure value appears in any rendered UI surface (SC-003).
