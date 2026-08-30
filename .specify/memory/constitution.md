<!--
## Sync Impact Report

**Version change**: 1.0.0 → 1.0.1
**Rationale**: PATCH — Principle III target toolchain updated ESP-IDF v6.0.2 → v6.1
(non-semantic version refresh); added a note that `sdkconfig` must be regenerated on
an IDF minor bump. No principle added, removed, or redefined.
**Templates**: no changes required (no template references the IDF version).
**Follow-ups**: README hardware/setup section updated to v6.1 in the same change.

---
### Prior: (blank template) → 1.0.0
**Rationale**: MINOR bump from template placeholders to first substantive version; all sections newly authored.

### Modified Principles
- All five principle slots: blank template → concrete project principles (initial authoring, not renames)

### Added Sections
- I. ESP-IDF Component Architecture
- II. Hardware Abstraction Layer
- III. Build Integrity (NON-NEGOTIABLE)
- IV. Embedded Resource Discipline
- V. Network & Security Standards
- Hardware Platform Standards
- Development Workflow & Git Standards

### Removed Sections
- None (first real fill)

### Templates Status
- `.specify/templates/plan-template.md` — ✅ Constitution Check gate references are generic; no updates required
- `.specify/templates/spec-template.md` — ✅ Scope/requirements placeholders are generic; no updates required
- `.specify/templates/tasks-template.md` — ✅ Task categorization is generic; no updates required
- `.specify/templates/commands/` — ✅ Directory does not exist; nothing to update
- `README.md` — ⚠ Pending: still references ESP-IDF v5.4.x and DS18B20; update when spec-005 ships

### Deferred items
- `RATIFICATION_DATE` set to first-fill date (2026-07-21); update to actual project start date if known
- README hardware table still shows DS18B20 — update when BMP280 migration is complete
-->

# ESP32 Weather Station Constitution

## Core Principles

### I. ESP-IDF Component Architecture

All firmware code MUST reside in properly structured ESP-IDF components under `components/`.
Each component MUST provide a `CMakeLists.txt` that declares its sources, include paths, and
explicit `REQUIRES` / `PRIV_REQUIRES` dependencies — no implicit include-path leakage.

Cross-component communication MUST go through the shared `app_ctx` context structure
(queues, task handles, shared state). Components MUST NOT call each other's private functions
or reference each other's internal headers.

Every component MUST expose a minimal, documented public API via its top-level header.
Component boundaries MUST reflect hardware or domain ownership (sensor, display, web_server,
history, etc.) — no catch-all or "utils" components.

**Rationale**: ESP-IDF's CMake component model is the sanctioned unit of reuse and isolation.
Violating component boundaries causes circular dependencies that break `idf.py build` and
make firmware impossible to unit-test on host.

### II. Hardware Abstraction Layer

Peripheral drivers (display, sensor, buttons) MUST be fully encapsulated in their dedicated
components. GPIO pin numbers, SPI bus handles, and register-level sequences MUST NOT appear
in any component other than the driver that owns that peripheral.

Pin assignments MUST be centralized in `sdkconfig` (via Kconfig options) or a single
board-configuration header; no magic numbers in `.c` files.

Button debounce MUST be implemented in software within the `display` component; raw GPIO
interrupts MUST NOT propagate outside the driver.

**Rationale**: Hardware pin assignments change between board revisions. Centralizing them
means a single-line edit to port the firmware rather than a grep-and-replace across all
source files.

### III. Build Integrity (NON-NEGOTIABLE)

Every commit merged to `main` MUST produce a clean `idf.py build` with zero compiler errors
and zero warnings. Warnings MUST NOT be silenced with pragmas unless accompanied by a comment
explaining the upstream defect and linking to the IDF issue tracker.

The target toolchain is ESP-IDF v6.1 with the `esp32` target. Any change to `sdkconfig`
defaults MUST be accompanied by a verified rebuild. When bumping the ESP-IDF minor
version, the gitignored `sdkconfig` MUST be regenerated from `sdkconfig.defaults`
(delete it, or `idf.py fullclean`) — a stale `sdkconfig` silently overrides renamed or
newly-defaulted options (e.g. it has masked `CONFIG_HTTPD_WS_SUPPORT` before).

**Rationale**: Embedded firmware that does not compile is completely non-functional. This gate
is the minimum bar for any change landing on `main`.

### IV. Embedded Resource Discipline

- **Memory**: All heap allocations MUST be bounded and justified. `malloc` MUST NOT be called
  from ISR context. Buffers with fixed maximum sizes SHOULD be declared statically.
- **Flash wear**: NVS and history writes MUST be batched (history: hourly batch writes).
  No per-sample flash write is permitted.
- **FreeRTOS tasks**: Every task MUST declare an explicit stack size. Stack high-water marks
  MUST be checked during development and documented in the relevant component's comments when
  the margin is under 512 bytes.
- **Sensor sampling**: Sensor reads MUST be periodic (≥ 5 s interval) and MUST NOT block the
  display or web_server tasks.

**Rationale**: The ESP32-D0WDQ6 has 520 KB of internal SRAM and no PSRAM on this board.
Unbounded allocations or tight-loop flash writes will produce hard-to-diagnose panics or
premature flash wear-out.

### V. Network & Security Standards

- **AP fallback**: The device MUST enter captive-portal AP mode (`weather-XXXX`) if WiFi
  credentials are absent or STA connection fails after the configured retry count.
- **HTTPS only**: The management interface MUST use HTTPS backed by a device certificate
  signed by the project CA. Plain HTTP MUST be rejected for management endpoints.
- **No plaintext secrets**: WiFi passwords and TLS private keys MUST be stored only in NVS
  (preferably encrypted NVS). They MUST NOT appear in flash outside NVS or in log output.
- **OTA safety**: OTA uploads MUST be verified (image hash check) and the device MUST
  roll back automatically on boot failure of a newly flashed image.

**Rationale**: The device operates on a home network but exposes an HTTPS management page
accessible from any LAN client. Weak defaults here create a trivially exploitable device.

## Hardware Platform Standards

**Target SoC**: ESP32-D0WDQ6 (dual-core Xtensa LX6, 520 KB SRAM, no PSRAM)

**Flash**: 25Q128JVSIQ — 128 Mbit (16 MB) SPI NOR flash; OTA dual-partition layout required.

**Display**: ST7789 TFT 135×240 px via SPI; landscape orientation; driven by `display` component.

**Sensor**: BMP280/BME280 via SPI; provides temperature, atmospheric pressure, and (BME280 only)
relative humidity; driven by `sensor` component.

**Buttons**: Left (UTC/local toggle; long-press: factory reset) and Right (°C/°F toggle;
long-press: transient WiFi details screen — SSID, IP, mDNS hostname); debounced in `display`
component; settings persist to NVS across reboots.

All pin assignments for the above peripherals MUST be defined in `sdkconfig` or a dedicated
board-config header — never as raw integer literals in component source.

## Development Workflow & Git Standards

**Branching**: Features MUST be developed on named branches (`###-feature-name`) and merged
to `main` via a pull request. Force-push to `main` is prohibited.

**Commit discipline**: Every commit MUST compile cleanly (Principle III). Commit messages MUST
follow Conventional Commits style (`feat:`, `fix:`, `docs:`, `refactor:`, etc.). "WIP" or
undescriptive commits (e.g., "fix stuff") MUST be squashed before merging to `main`.

**Spec-first workflow**: Non-trivial features MUST be captured in `specs/###-feature-name/`
(spec → plan → tasks) before implementation begins. The spec number MUST match the feature
branch prefix.

**Testing**: Where the ESP-IDF host test framework supports it, business-logic components
(history codec, i18n, data parsing) MUST have unit tests runnable with `idf.py -C
components/<name>/test build`. Hardware-dependent components MAY use the hardware emulator
(spec 003) for integration testing.

## Governance

This constitution supersedes all informal agreements and takes precedence over any individual
component's README or inline comments in the event of conflict.

**Amendment procedure**:
1. Open a PR describing the proposed change and rationale.
2. Increment `CONSTITUTION_VERSION` following semantic versioning:
   - MAJOR — backward-incompatible removal or redefinition of an existing principle.
   - MINOR — new principle or section added; materially expanded guidance.
   - PATCH — clarification, wording fix, or non-semantic refinement.
3. Update `LAST_AMENDED_DATE` to the merge date.
4. Propagate any principle changes to dependent templates (plan, spec, tasks).

**Compliance**: All PRs to `main` MUST pass the Constitution Check gate in the plan template
before implementation. Complexity violations (Principle IV exceptions, security exceptions)
MUST be documented in the Complexity Tracking table of the relevant plan.

**Version**: 1.0.1 | **Ratified**: 2026-07-21 | **Last Amended**: 2026-08-30
