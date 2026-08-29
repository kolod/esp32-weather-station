# Implementation Plan: Local Hardware Emulator for Web UI Testing

**Branch**: `003-hardware-emulator` | **Date**: 2026-07-12 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/003-hardware-emulator/spec.md`

## Summary

A single-file Python script (`tools/hw_emulator.py`, standard library only — no frameworks, no pip installs) that runs two local HTTP endpoints mirroring the ESP32 weather station's management server and captive-portal server. It serves the real page assets (`components/web_server/www/`) straight from disk on every request, implements every API the pages call with device-equivalent JSON shapes and state machines (join flow, OTA flow), and opens both pages in the default browser on startup. Failure/edge scenarios are set via CLI flags and switchable at runtime through a small emulator-only control endpoint. The web pages themselves are **never modified** for the emulator — they must keep working unchanged against real hardware.

**User constraint (this plan invocation)**: minimize big frameworks/servers; the pages must remain simple, fast, and fully compatible with the real ESP32 firmware once web development is done. → Emulator is stdlib-only (`http.server`, `json`, `argparse`, `webbrowser`, `threading`); zero changes to `www/` assets; zero new build steps.

## Technical Context

**Language/Version**: Python 3.9+ (standard library only)

**Primary Dependencies**: None (stdlib: `http.server`, `socketserver`, `json`, `argparse`, `threading`, `webbrowser`, `time`, `math`, `csv`, `pathlib`)

**Storage**: None — all emulated state in memory, reset on restart (per spec Assumptions)

**Testing**: `unittest` (stdlib) for pure logic (Accept-Language picker, scenario/state machines, synthetic data generators); manual browser validation via `quickstart.md`

**Target Platform**: Developer workstations (Windows/macOS/Linux) with a stock Python 3 install

**Project Type**: Single-file developer CLI tool

**Performance Goals**: Page load + all API calls well under 1 s locally; supports several browser tabs polling concurrently (threading server)

**Constraints**: Localhost-only binding (FR-001); no modification of `components/web_server/www/**`; response shapes byte-compatible in structure with firmware handlers; assets re-read from disk on every request (FR-003)

**Scale/Scope**: 2 HTTP servers, ~16 device routes + captive probes + emulator control routes; ~1 developer at a time

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

`.specify/memory/constitution.md` is an unfilled template — no ratified principles exist. No gates to enforce. **PASS** (vacuous). The user constraint "minimize frameworks" is treated as a binding principle for this feature and is satisfied by the stdlib-only decision.

## Project Structure

### Documentation (this feature)

```text
specs/003-hardware-emulator/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── mgmt-api.md      # Management endpoint contract (mirrors firmware)
│   ├── portal-api.md    # Portal endpoint contract (mirrors firmware)
│   └── emulator-control.md  # Emulator-only control endpoints (/emu/*)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
tools/
├── hw_emulator.py        # The emulator (single file, stdlib only)
└── test_hw_emulator.py   # unittest for pure logic (lang pick, state machines, generators)

components/web_server/www/   # EXISTING page sources — served as-is, never modified
├── mgmt/    (mgmt.html, mgmt.css, mgmt.js)
├── portal/  (index.html, portal.css, portal.js)
└── i18n/    (en.json, de.json, fr.json, uk.json)
```

**Structure Decision**: A new `tools/` directory at repo root holds the emulator and its test file. Nothing under `components/` changes. The emulator locates `www/` relative to its own path (`../components/web_server/www`), so it works when launched from any working directory, and errors clearly if the assets are missing (spec edge case).

## Design Decisions (summary — details in research.md)

| # | Decision | Choice |
|---|----------|--------|
| D1 | HTTP stack | `http.server.ThreadingHTTPServer` + `BaseHTTPRequestHandler`, one instance per port, both on `127.0.0.1` |
| D2 | Ports | Management `8080`, portal `8081`; `--mgmt-port` / `--portal-port` overrides; clear error if a port is busy |
| D3 | TLS | None. Device mgmt server is HTTPS :443, but pages use only relative URLs → plain HTTP locally is behavior-equivalent |
| D4 | Scenario control | CLI flags set initial state; runtime switching via `GET/PUT /emu/scenario` (JSON) + minimal built-in control page at `/emu` on the mgmt port |
| D5 | Browser auto-open | `webbrowser.open()` for both page URLs after both sockets bind (FR-014); `--no-browser` opt-out |
| D6 | Caching | `Cache-Control: no-cache` on all asset responses (deviation from device's `max-age=3600` on css/js, justified by FR-003 live-edit requirement) |
| D7 | i18n | Port of firmware `accept_language_pick()` q-value logic; serve `/i18n/<lang>.json` from disk; inject `data-lang` into portal HTML on the fly (implements firmware's *documented intent*; actual firmware currently 404s these — gap flagged in research.md R8) |
| D8 | Captive probes | `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt` → `302` to the emulator portal root (device redirects to `http://192.168.4.1/`) |
| D9 | Synthetic data | Deterministic-but-varying temperature (sinusoid + noise), 24 h history at 60 s intervals by default (`--history-hours`), fixed scan list with designated outcome SSIDs |
| D10 | Join/OTA state machines | Timed transitions matching device semantics: join `connecting → connected/failed` after ~2 s; OTA `receiving(progress) → validating → applied_pending_reboot → (3 s) → simulated reboot` (uptime resets, state → `idle`) |

## Complexity Tracking

No constitution violations; table not applicable.
