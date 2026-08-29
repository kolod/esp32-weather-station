#!/usr/bin/env python3
"""Local hardware emulator for the ESP32 weather station web UIs.

Runs two HTTP endpoints on localhost that mirror the device's management
server and captive-portal server, serving the real page sources from
components/web_server/www/ (re-read on every request, so edits show up on
browser refresh). All device APIs are emulated with response shapes matching
the firmware (components/web_server/handlers_*.c, portal_server.c).

Usage:
    python tools/hw_emulator.py
    python tools/hw_emulator.py --no-browser --mgmt-port 9000 --portal-port 9001
    python tools/hw_emulator.py --join-outcome auth --sensor-invalid

Runtime scenario switching: open http://127.0.0.1:<mgmt-port>/emu
Design docs: specs/003-hardware-emulator/ (spec, plan, contracts, quickstart).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import threading
import time
import webbrowser
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

WWW_ROOT = Path(__file__).resolve().parent.parent / "components" / "web_server" / "www"

SUPPORTED_LANGS = ("en", "de", "fr", "uk")
DEFAULT_LANG = "en"

OTA_MAX_SIZE = 3 * 1024 * 1024  # matches handlers_ota.c
HISTORY_INTERVAL_S = 60
UINT32_MAX = 0xFFFFFFFF

JOIN_OUTCOMES = ("success", "auth", "not_found")
OTA_OUTCOMES = ("success", "invalid_image", "write_error")
SENSOR_KINDS = ("bme280", "bmp280", "ds18b20", "none")  # spec 005/007 boot-time fittings
BOOTLOG_STATES = ("present", "missing")       # spec 006 boot-log availability

# Joining these SSIDs forces an outcome regardless of the scenario setting,
# so portal failure paths are reproducible straight from the page.
DESIGNATED_SSIDS = {"Emu-WrongPass": "auth", "Emu-NotFound": "not_found"}

SCAN_NETWORKS = [
    {"ssid": "HomeNet", "rssi": -45, "secure": True},
    {"ssid": "Emu-WrongPass", "rssi": -52, "secure": True},
    {"ssid": "IoT-Net", "rssi": -58, "secure": True},
    {"ssid": "Office-Guest", "rssi": -60, "secure": True},
    {"ssid": "Neighbors 5G", "rssi": -67, "secure": True},
    {"ssid": "CoffeeShop Free", "rssi": -70, "secure": False},
    {"ssid": "PrinterSetup-8FA2", "rssi": -75, "secure": False},
    {"ssid": "Emu-NotFound", "rssi": -80, "secure": True},
]

# Representative subset of the firmware tz_table (single source for both
# /api/timezones and timezone validation — see research.md R6).
TIMEZONES = (
    "UTC",
    "Europe/Kyiv", "Europe/Berlin", "Europe/London", "Europe/Paris",
    "Europe/Madrid", "Europe/Rome", "Europe/Warsaw", "Europe/Prague",
    "Europe/Amsterdam", "Europe/Stockholm", "Europe/Lisbon",
    "America/New_York", "America/Chicago", "America/Denver",
    "America/Los_Angeles", "America/Sao_Paulo", "America/Mexico_City",
    "Asia/Tokyo", "Asia/Shanghai", "Asia/Kolkata", "Asia/Dubai",
    "Asia/Singapore", "Australia/Sydney", "Pacific/Auckland",
)

MIME_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css",
    ".js": "application/javascript",
    ".json": "application/json",
}

SCENARIO_VALIDATORS = {
    "join_outcome": lambda v: v in JOIN_OUTCOMES,
    "ota_outcome": lambda v: v in OTA_OUTCOMES,
    "sensor": lambda v: v in SENSOR_KINDS,
    "sensor_valid": lambda v: isinstance(v, bool),
    "time_synced": lambda v: isinstance(v, bool),
    "wifi_connected": lambda v: isinstance(v, bool),
    "storage_free_kb": lambda v: isinstance(v, int) and not isinstance(v, bool) and v >= 0,
    "bootlog": lambda v: v in BOOTLOG_STATES,
}

# Synthetic /api/boot.log content (spec 006): ESP-IDF-format lines matching
# the firmware messages emitted by sensor detection (sensor.c/bmp280.c) and
# display init (display.c), per boot-time sensor fitting.
BOOTLOG_SENSOR_LINES = {
    "bme280": (
        "I (1621) bmp280: BME280 found at 0x76",
        "I (1633) sensor: Sensor mode: BME280 (temperature + pressure + humidity)",
    ),
    "bmp280": (
        "I (1621) bmp280: BMP280 found at 0x76",
        "I (1633) sensor: Sensor mode: BMP280 (temperature + pressure)",
    ),
    "ds18b20": (
        "W (1621) bmp280: BMP280 not found at 0x76 or 0x77",
        "I (1745) sensor: DS18B20 found on GPIO27, address: 28FF4C1D93160432",
        "I (1751) sensor: Sensor mode: DS18B20 (temperature only)",
    ),
    "none": (
        "W (1621) bmp280: BMP280 not found at 0x76 or 0x77",
        "W (1789) sensor: No DS18B20 device found on bus",
        "I (1795) sensor: Sensor mode: none detected",
    ),
}

BOOTLOG_DISPLAY_LINES = (
    "I (2104) display: SPI bus and panel IO created",
    "I (2287) display: ST7789 panel initialized (250x135)",
    "I (2311) display: LVGL port ready",
    "I (2350) display: UI initialized",
)

JOIN_RESOLVE_DELAY_S = 2.0   # connecting -> connected/failed
OTA_REBOOT_DELAY_S = 3.0     # applied_pending_reboot -> simulated reboot
SCAN_DELAY_S = 1.0           # keeps the page's "Scanning…" state visible


def accept_language_pick(header):
    """Pick a supported language from an Accept-Language header.

    Port of components/web_server/i18n.c: primary-subtag match against
    SUPPORTED_LANGS, highest q wins, fallback to 'en'.
    """
    if not header:
        return DEFAULT_LANG
    best_lang, best_q = DEFAULT_LANG, -1.0
    for token in header.split(","):
        token = token.strip()
        if not token:
            continue
        parts = token.split(";")
        primary = parts[0].strip().split("-")[0].lower()
        q = 1.0
        for param in parts[1:]:
            param = param.strip()
            if param.startswith("q="):
                try:
                    q = float(param[2:])
                except ValueError:
                    q = 0.0
        if primary in SUPPORTED_LANGS and q > best_q:
            best_q, best_lang = q, primary
    return best_lang


def temperature_at(epoch):
    """Deterministic simulated temperature: daily sinusoid 21±4 °C plus
    per-minute noise ≤0.3 °C (research.md R6)."""
    minute = int(epoch // HISTORY_INTERVAL_S)
    noise = math.sin(minute * 12.9898) * 0.3
    day_phase = (epoch % 86400) / 86400.0
    return 21.0 + 4.0 * math.sin(2 * math.pi * day_phase) + noise


def pressure_at(epoch):
    """Deterministic simulated station pressure: ~2-day sinusoid 1013±8 hPa
    plus per-minute noise ≤0.2 hPa (spec 005, research D8)."""
    minute = int(epoch // HISTORY_INTERVAL_S)
    noise = math.sin(minute * 7.8233) * 0.2
    phase = (epoch % (2 * 86400)) / (2 * 86400.0)
    return 1013.0 + 8.0 * math.sin(2 * math.pi * phase) + noise


def humidity_at(epoch):
    """Deterministic simulated relative humidity: ~1-day sinusoid 50±20 %RH
    plus per-minute noise ≤0.5 %RH, clamped to [0, 100] (spec 007, research D7)."""
    minute = int(epoch // HISTORY_INTERVAL_S)
    noise = math.sin(minute * 4.1357) * 0.5
    day_phase = (epoch % 86400) / 86400.0
    return max(0.0, min(100.0, 50.0 + 20.0 * math.sin(2 * math.pi * day_phase) + noise))


class Emulator:
    """All emulated device state; one instance shared by both servers.

    Methods take an optional `now` (epoch seconds) so tests can inject a
    clock. State transitions are resolved lazily on read (`_tick`) — no
    timer threads needed.
    """

    def __init__(self, history_hours=24, join_outcome="success",
                 ota_outcome="success", sensor="bmp280", sensor_valid=True,
                 time_synced=True, wifi_connected=True, storage_free_kb=1024,
                 bootlog="present", now=None):
        now = time.time() if now is None else now
        self.lock = threading.RLock()
        self.boot_time = now
        self.device_suffix = "A1B2"
        self.fw_build = 0

        # Which fitting produced each history window (spec 005): segments of
        # (start_epoch, sensor_kind), appended when the scenario switches.
        self.sensor_segments = [(0, sensor)]

        self.tz_name = "UTC"
        self.time_mode = "local"
        self.temp_unit = "C"

        self.history_start = (int(now) // HISTORY_INTERVAL_S) * HISTORY_INTERVAL_S \
            - history_hours * 3600

        self.scenario = {
            "join_outcome": join_outcome,
            "ota_outcome": ota_outcome,
            "sensor": sensor,
            "sensor_valid": sensor_valid,
            "time_synced": time_synced,
            "wifi_connected": wifi_connected,
            "storage_free_kb": storage_free_kb,
            "bootlog": bootlog,
        }

        # Join session (data-model.md JoinSession)
        self.join_state = "idle"       # idle | connecting | connected | failed
        self.join_reason = None        # "auth" | "not_found" | None
        self.join_resolve_at = 0.0
        self.join_pending_outcome = "success"

        # OTA session (data-model.md OtaSession)
        self.ota_state = "idle"        # idle|receiving|validating|applied_pending_reboot|failed
        self.ota_progress = 0
        self.ota_error = ""
        self.ota_reboot_at = None

    # ── internals ───────────────────────────────────────────────────────

    @property
    def fw_version(self):
        return f"emu-0.1.{self.fw_build}"

    def _tick(self, now):
        """Resolve timed transitions; caller must hold the lock."""
        if self.join_state == "connecting" and now >= self.join_resolve_at:
            if self.join_pending_outcome == "success":
                self.join_state, self.join_reason = "connected", None
            else:
                self.join_state = "failed"
                self.join_reason = self.join_pending_outcome
        if self.ota_state == "applied_pending_reboot" and self.ota_reboot_at is not None \
                and now >= self.ota_reboot_at:
            # Simulated reboot: uptime restarts, "new firmware" runs.
            self.boot_time = now
            self.fw_build += 1
            self.ota_state, self.ota_progress, self.ota_error = "idle", 0, ""
            self.ota_reboot_at = None

    # ── history ─────────────────────────────────────────────────────────

    def history_records(self, from_ts=0, to_ts=UINT32_MAX, now=None):
        """Records are a pure function of time — nothing is stored."""
        now = time.time() if now is None else now
        with self.lock:
            start = self.history_start
        last = (int(now) // HISTORY_INTERVAL_S) * HISTORY_INTERVAL_S
        lo = max(start, ((from_ts + HISTORY_INTERVAL_S - 1) // HISTORY_INTERVAL_S)
                 * HISTORY_INTERVAL_S)
        hi = min(last, to_ts)
        return [(ts, round(temperature_at(ts), 2))
                for ts in range(lo, hi + 1, HISTORY_INTERVAL_S)]

    def history_count(self, now=None):
        now = time.time() if now is None else now
        with self.lock:
            start = self.history_start
        last = (int(now) // HISTORY_INTERVAL_S) * HISTORY_INTERVAL_S
        return max(0, (last - start) // HISTORY_INTERVAL_S + 1)

    def sensor_at(self, ts):
        """Fitting that was active at epoch ts (walks scenario segments)."""
        with self.lock:
            kind = self.sensor_segments[0][1]
            for start, seg_kind in self.sensor_segments:
                if ts >= start:
                    kind = seg_kind
                else:
                    break
            return kind

    def history_pressure_at(self, ts):
        """Pressure recorded with the sample at ts, or None (spec 005/007)."""
        return (round(pressure_at(ts), 1)
                if self.sensor_at(ts) in ("bmp280", "bme280") else None)

    def history_humidity_at(self, ts):
        """Humidity recorded with the sample at ts, or None (spec 007:
        only while a BME280 was the active fitting)."""
        return round(humidity_at(ts), 1) if self.sensor_at(ts) == "bme280" else None

    # ── management API ──────────────────────────────────────────────────

    def status(self, now=None):
        now = time.time() if now is None else now
        with self.lock:
            self._tick(now)
            sc = self.scenario
            synced = sc["time_synced"]
            wifi_ok = sc["wifi_connected"]
            sensor = sc["sensor"]
            return {
                "temperature_c": round(temperature_at(now), 2),
                "temperature_valid": sc["sensor_valid"] and sensor != "none",
                "pressure_hpa": round(pressure_at(now), 1),
                "pressure_valid": sc["sensor_valid"] and sensor in ("bmp280", "bme280"),
                "humidity_pct": round(humidity_at(now), 1),
                "humidity_valid": sc["sensor_valid"] and sensor == "bme280",
                "sensor": sensor,
                "time_synced": synced,
                "time_source": "ntp" if synced else "none",
                "time_last_sync": int(now - 3600) if synced else None,
                "now": int(now),
                "tz_name": self.tz_name,
                "time_mode": self.time_mode,
                "temp_unit": self.temp_unit,
                "wifi": {
                    "state": "connected" if wifi_ok else "retrying",
                    "ssid": "HomeNet" if wifi_ok else "",
                    "rssi": -47 if wifi_ok else 0,
                    "ip": "192.168.1.42" if wifi_ok else "",
                },
                "fw_version": self.fw_version,
                "uptime_s": max(0, int(now - self.boot_time)),
                "history_records": self.history_count(now),
                "storage_free_kb": sc["storage_free_kb"],
            }

    def apply_config(self, cfg):
        """Apply tz/mode/unit in firmware order; first failure stops
        processing but earlier fields stay applied (handlers_mgmt.c).
        Returns an error code string or None."""
        def field(key):
            v = cfg.get(key)
            return v if isinstance(v, str) else ""

        with self.lock:
            tz = field("tz_name")
            if tz:
                if tz not in TIMEZONES:
                    return "unknown_timezone"
                self.tz_name = tz
            mode = field("time_mode")
            if mode:
                if mode not in ("utc", "local"):
                    return "invalid_time_mode"
                self.time_mode = mode
            unit = field("temp_unit")
            if unit:
                if unit not in ("C", "F"):
                    return "invalid_temp_unit"
                self.temp_unit = unit
        return None

    # ── portal API ──────────────────────────────────────────────────────

    def start_join(self, body, now=None):
        """Validate a POST /api/wifi body and start a join session.
        Returns (http_status, payload) matching portal_server.c."""
        now = time.time() if now is None else now
        if not body or not body.strip():
            return 400, {"error": "empty_body"}
        try:
            data = json.loads(body.decode("utf-8", "replace"))
        except ValueError:
            data = {}
        if not isinstance(data, dict):
            data = {}

        ssid = data.get("ssid")
        if not isinstance(ssid, str) or not 1 <= len(ssid) <= 32:
            return 400, {"error": "invalid_ssid"}
        password = data.get("password")
        if isinstance(password, str) and len(password) > 63:
            return 400, {"error": "password_too_long"}

        # Valid timezone applied, invalid silently ignored (firmware behavior)
        tz = data.get("tz_name")
        with self.lock:
            if isinstance(tz, str) and tz in TIMEZONES:
                self.tz_name = tz
            self.join_pending_outcome = DESIGNATED_SSIDS.get(
                ssid, self.scenario["join_outcome"])
            self.join_state = "connecting"
            self.join_reason = None
            self.join_resolve_at = now + JOIN_RESOLVE_DELAY_S
        return 202, {"status": "connecting"}

    def wifi_status(self, now=None):
        now = time.time() if now is None else now
        with self.lock:
            self._tick(now)
            if self.join_state == "connected":
                return {"state": "connected", "suffix": self.device_suffix,
                        "reason": None}
            if self.join_state == "connecting":
                return {"state": "connecting", "ip": None, "reason": None}
            # idle maps to "failed" with reason null, like JOIN_IDLE in firmware
            return {"state": "failed", "ip": None, "reason": self.join_reason}

    # ── OTA API ─────────────────────────────────────────────────────────

    def ota_begin(self, content_len):
        """Returns (http_status, payload) on rejection, None if accepted."""
        with self.lock:
            if self.ota_state == "receiving":
                return 409, {"error": "update_in_progress"}
            if content_len <= 0 or content_len > OTA_MAX_SIZE:
                return 507, {"error": "image_too_large"}
            self.ota_state, self.ota_progress, self.ota_error = "receiving", 0, ""
            self.ota_reboot_at = None
        return None

    def ota_progress_update(self, received, total):
        with self.lock:
            if self.ota_state == "receiving":
                self.ota_progress = min(100, received * 100 // total)

    def ota_validating(self):
        with self.lock:
            self.ota_state, self.ota_progress = "validating", 100

    def ota_finish(self, now=None):
        """Resolve per scenario. Returns (http_status, payload)."""
        now = time.time() if now is None else now
        with self.lock:
            outcome = self.scenario["ota_outcome"]
            if outcome == "invalid_image":
                self.ota_state, self.ota_progress = "failed", 0
                self.ota_error = "invalid_image"
                return 400, {"error": "invalid_image",
                             "message": "Image validation failed"}
            if outcome == "write_error":
                self.ota_state, self.ota_progress = "failed", 0
                self.ota_error = "write_error"
                return 500, {"error": "write_error"}
            self.ota_state, self.ota_progress = "applied_pending_reboot", 100
            self.ota_error = ""
            self.ota_reboot_at = now + OTA_REBOOT_DELAY_S
            return 200, {"status": "applied", "reboot_in_s": 3}

    def ota_fail(self, error):
        with self.lock:
            self.ota_state, self.ota_progress, self.ota_error = "failed", 0, error

    def ota_status(self, now=None):
        now = time.time() if now is None else now
        with self.lock:
            self._tick(now)
            return {"state": self.ota_state, "progress_pct": self.ota_progress,
                    "error": self.ota_error or None}

    # ── boot log (spec 006) ─────────────────────────────────────────────

    def bootlog_text(self):
        """Synthetic boot.log for the boot-time sensor fitting, or None when
        the scenario says the log is missing (contracts/bootlog-api.md)."""
        with self.lock:
            if self.scenario["bootlog"] != "present":
                return None
            sensor = self.scenario["sensor"]
        lines = BOOTLOG_SENSOR_LINES[sensor] + BOOTLOG_DISPLAY_LINES
        text = "\n".join(lines) + "\n"
        assert len(text.encode()) <= 4096  # file cap, SC-003
        return text

    # ── scenario control ────────────────────────────────────────────────

    def scenario_snapshot(self):
        with self.lock:
            return dict(self.scenario)

    def scenario_patch(self, patch, now=None):
        """Validate and merge. Returns error field name or None."""
        if not isinstance(patch, dict):
            return "(body)"
        for key, value in patch.items():
            validator = SCENARIO_VALIDATORS.get(key)
            if validator is None or not validator(value):
                return key
        with self.lock:
            if "sensor" in patch and patch["sensor"] != self.scenario["sensor"]:
                now = time.time() if now is None else now
                self.sensor_segments.append((int(now), patch["sensor"]))
            self.scenario.update(patch)
        return None


# ── Control page (/emu) — self-contained, no external assets ────────────

CONTROL_PAGE = """<!DOCTYPE html>
<html lang="en"><head><meta charset="UTF-8"><title>Emulator Control</title>
<style>
body{font-family:system-ui,sans-serif;max-width:26rem;margin:2rem auto;padding:0 1rem}
h2{font-size:1rem;margin:1.4rem 0 .6rem;border-bottom:1px solid #ccc;padding-bottom:.3rem}
label{display:block;margin:.6rem 0}select,input[type=number]{margin-left:.4rem}
#msg,#msg-cfg{margin-top:.6rem;font-size:.9rem}
button{margin-top:.8rem;padding:.4rem 1.2rem}
</style></head><body>
<h1>Emulator scenario</h1>

<h2>Simulation outcomes</h2>
<label>Join outcome
 <select id="join_outcome"><option>success</option><option>auth</option><option>not_found</option></select>
</label>
<label>OTA outcome
 <select id="ota_outcome"><option>success</option><option>invalid_image</option><option>write_error</option></select>
</label>
<label>Sensor fitting
 <select id="sensor"><option>bme280</option><option>bmp280</option><option>ds18b20</option><option>none</option></select>
</label>
<label><input type="checkbox" id="sensor_valid"> Sensor reading valid</label>
<label><input type="checkbox" id="time_synced"> Time synced</label>
<label><input type="checkbox" id="wifi_connected"> WiFi connected</label>
<label>Storage free (kB) <input type="number" id="storage_free_kb" min="0"></label>
<label>Boot log
 <select id="bootlog"><option>present</option><option>missing</option></select>
</label>
<button id="apply">Apply scenario</button>
<div id="msg"></div>

<h2>Device config</h2>
<label><input type="checkbox" id="cfg_utc"> Show time as UTC (unchecked = LOCAL)</label>
<label><input type="checkbox" id="cfg_f"> Show temperature in °F (unchecked = °C)</label>
<button id="apply-cfg">Apply config</button>
<div id="msg-cfg"></div>

<script>
'use strict';
const F=["join_outcome","ota_outcome","sensor","sensor_valid","time_synced","wifi_connected","storage_free_kb","bootlog"];
const $=id=>document.getElementById(id);
function render(s){F.forEach(k=>{const el=$(k);
 if(el.type==="checkbox")el.checked=s[k];else el.value=s[k];});}
async function load(){
  render(await (await fetch('/emu/scenario')).json());
  const st=await (await fetch('/api/status')).json();
  $('cfg_utc').checked = st.time_mode === 'utc';
  $('cfg_f').checked   = st.temp_unit === 'F';
}
$('apply').addEventListener('click',async()=>{
 const p={};F.forEach(k=>{const el=$(k);
  p[k]=el.type==="checkbox"?el.checked:(el.type==="number"?parseInt(el.value,10)||0:el.value);});
 const r=await fetch('/emu/scenario',{method:'PUT',
  headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});
 $('msg').textContent=r.ok?'Applied.':'Error: '+await r.text();
 if(r.ok)render(await r.json());});
$('apply-cfg').addEventListener('click',async()=>{
 const cfg={time_mode:$('cfg_utc').checked?'utc':'local',
             temp_unit:$('cfg_f').checked?'F':'C'};
 const r=await fetch('/api/config',{method:'PUT',
  headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});
 $('msg-cfg').textContent=r.ok?'Applied.':'Error: '+await r.text();});
load();
</script></body></html>"""


# ── HTTP layer ───────────────────────────────────────────────────────────

class EmuServer(ThreadingHTTPServer):
    daemon_threads = True
    # On Windows SO_REUSEADDR lets two processes bind the same port without
    # error, which would silently hide the "port already in use" failure.
    allow_reuse_address = sys.platform != "win32"

    def __init__(self, address, handler_cls, emu, label):
        self.emu = emu
        self.label = label
        super().__init__(address, handler_cls)


class BaseEmuHandler(BaseHTTPRequestHandler):
    """Route-table dispatch; unknown paths get a 404 plus a MISS log line."""

    server_version = "esp32-weather-emu/0.1"
    ROUTES = {}
    SERVE_I18N = True

    # ── plumbing ────────────────────────────────────────────────────────

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")

    def do_PUT(self):
        self._dispatch("PUT")

    def _dispatch(self, method):
        parsed = urlparse(self.path)
        self.route_path = parsed.path
        self.query = parse_qs(parsed.query)
        name = self.ROUTES.get((method, parsed.path))
        if name is not None:
            getattr(self, name)()
            return
        if self.SERVE_I18N and method == "GET":
            if parsed.path == "/i18n.js":
                self.serve_www("common/i18n.js")
                return
            m = re.fullmatch(r"/i18n/([a-z]{2})\.json", parsed.path)
            if m and m.group(1) in SUPPORTED_LANGS:
                self.serve_www(f"i18n/{m.group(1)}.json")
                return
        self.log_message("MISS %s %s", method, parsed.path)
        self.send_json(404, {"error": "not_found"})

    def log_message(self, fmt, *args):
        sys.stderr.write(f"[{self.server.label}] {fmt % args}\n")

    @property
    def emu(self):
        return self.server.emu

    def read_body(self):
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = 0
        return self.rfile.read(length) if length > 0 else b""

    def send_body(self, code, content_type, body, headers=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, code, obj, headers=None):
        self.send_body(code, "application/json", json.dumps(obj), headers)

    def serve_www(self, rel_path, headers=None, transform=None):
        path = WWW_ROOT / rel_path
        try:
            data = path.read_bytes()  # re-read every request: live edit loop
        except OSError:
            self.log_message("missing asset: %s", path)
            self.send_json(404, {"error": "not_found"})
            return
        if transform is not None:
            data = transform(data)
        mime = MIME_TYPES.get(path.suffix, "application/octet-stream")
        all_headers = {"Cache-Control": "no-cache"}
        all_headers.update(headers or {})
        self.send_body(200, mime, data, all_headers)

    # ── shared device routes ────────────────────────────────────────────

    def h_timezones(self):
        self.send_json(200, [{"name": name} for name in TIMEZONES],
                       {"Cache-Control": "max-age=3600"})

    def serve_page_localized(self, rel_path, extra_headers=None):
        """Serve an HTML page with its <html lang="…"> value patched to the
        language picked from Accept-Language, and a matching
        Content-Language header — mirrors send_html_lang_patched() in
        components/web_server/handlers_common.c
        (specs/004-fix-web-i18n/contracts/i18n-http.md §1). The firmware
        considers at most 255 chars of the header, hence the slice."""
        lang = accept_language_pick(
            (self.headers.get("Accept-Language") or "")[:255])

        def patch(data):
            return re.sub(rb'(<html[^>]*\blang=")[A-Za-z-]*(")',
                          lambda m: m.group(1) + lang.encode() + m.group(2),
                          data, count=1)

        headers = {"Content-Language": lang}
        headers.update(extra_headers or {})
        self.serve_www(rel_path, headers, transform=patch)


class MgmtHandler(BaseEmuHandler):
    """Management endpoint — mirrors handlers_mgmt.c / handlers_ota.c
    (contracts/mgmt-api.md) plus the /emu control surface."""

    ROUTES = {
        ("GET", "/"): "h_index",
        ("GET", "/mgmt.css"): "h_css",
        ("GET", "/mgmt.js"): "h_js",
        ("GET", "/api/status"): "h_status",
        ("PUT", "/api/config"): "h_config",
        ("GET", "/api/history"): "h_history",
        ("GET", "/api/history.csv"): "h_history_csv",
        ("GET", "/api/boot.log"): "h_bootlog",
        ("GET", "/api/timezones"): "h_timezones",
        ("POST", "/api/ota"): "h_ota_post",
        ("GET", "/api/ota/status"): "h_ota_status",
        ("GET", "/emu"): "h_control_page",
        ("GET", "/emu/scenario"): "h_scenario_get",
        ("PUT", "/emu/scenario"): "h_scenario_put",
    }

    def h_index(self):
        # CSP/nosniff as on device; HSTS deliberately omitted (no TLS here)
        self.serve_page_localized("mgmt/mgmt.html", {
            "Content-Security-Policy":
                "upgrade-insecure-requests; default-src 'self'",
            "X-Content-Type-Options": "nosniff",
        })

    def h_css(self):
        self.serve_www("mgmt/mgmt.css")

    def h_js(self):
        self.serve_www("mgmt/mgmt.js")

    def h_status(self):
        self.send_json(200, self.emu.status())

    def h_config(self):
        body = self.read_body()
        try:
            cfg = json.loads(body.decode("utf-8", "replace")) if body.strip() else {}
        except ValueError:
            cfg = {}
        if not isinstance(cfg, dict):
            cfg = {}
        error = self.emu.apply_config(cfg)
        if error is not None:
            self.send_json(400, {"error": error})
            return
        self.send_json(200, self.emu.status())

    def _history_bound(self, key, default):
        try:
            return int(self.query[key][0])
        except (KeyError, IndexError, ValueError):
            return default

    def h_history(self):
        records = self.emu.history_records(self._history_bound("from", 0),
                                           self._history_bound("to", UINT32_MAX))
        self.send_json(200, {"records": [
            {"timestamp": ts, "temperature": temp,
             "pressure": self.emu.history_pressure_at(ts),
             "humidity": self.emu.history_humidity_at(ts)}
            for ts, temp in records]})

    def h_history_csv(self):
        lines = ["timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct"]
        for ts, temp in self.emu.history_records():
            iso = datetime.fromtimestamp(ts, timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%SZ")
            press = self.emu.history_pressure_at(ts)
            hum = self.emu.history_humidity_at(ts)
            p_cell = f"{press:.1f}" if press is not None else ""
            h_cell = f"{hum:.1f}" if hum is not None else ""
            lines.append(f"{iso},{temp:.2f},{p_cell},{h_cell}")
        self.send_body(200, "text/csv", "\n".join(lines) + "\n", {
            "Content-Disposition": 'attachment; filename="history.csv"'})

    def h_bootlog(self):
        text = self.emu.bootlog_text()
        if text is None:
            self.send_json(404, {"error": "not_found"})
            return
        self.send_body(200, "text/plain; charset=utf-8", text)

    def h_ota_post(self):
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = 0
        rejected = self.emu.ota_begin(length)
        if rejected is not None:
            code, payload = rejected
            self.send_json(code, payload, {"Connection": "close"})
            self.close_connection = True  # request body was not consumed
            return
        received = 0
        try:
            while received < length:
                chunk = self.rfile.read(min(32768, length - received))
                if not chunk:
                    raise ConnectionError("client aborted upload")
                received += len(chunk)
                self.emu.ota_progress_update(received, length)
                time.sleep(0.02)  # keep the receiving state observable
        except (ConnectionError, OSError):
            self.emu.ota_fail("write_error")
            self.close_connection = True
            return
        self.emu.ota_validating()
        time.sleep(0.4)  # let pollers see "validating"
        code, payload = self.emu.ota_finish()
        self.send_json(code, payload)

    def h_ota_status(self):
        self.send_json(200, self.emu.ota_status())

    def h_control_page(self):
        self.send_body(200, "text/html; charset=utf-8", CONTROL_PAGE,
                       {"Cache-Control": "no-cache"})

    def h_scenario_get(self):
        self.send_json(200, self.emu.scenario_snapshot())

    def h_scenario_put(self):
        body = self.read_body()
        try:
            patch = json.loads(body.decode("utf-8", "replace")) if body.strip() else {}
        except ValueError:
            patch = None
        bad_field = self.emu.scenario_patch(patch)
        if bad_field is not None:
            self.send_json(400, {"error": "invalid_scenario", "field": bad_field})
            return
        self.send_json(200, self.emu.scenario_snapshot())


class PortalHandler(BaseEmuHandler):
    """Captive-portal endpoint — mirrors portal_server.c
    (contracts/portal-api.md)."""

    PROBE_PATHS = ("/generate_204", "/gen_204", "/hotspot-detect.html",
                   "/connecttest.txt", "/ncsi.txt", "/redirect")

    ROUTES = {
        ("GET", "/"): "h_index",
        ("GET", "/portal.css"): "h_css",
        ("GET", "/portal.js"): "h_js",
        ("GET", "/api/scan"): "h_scan",
        ("POST", "/api/wifi"): "h_wifi_post",
        ("GET", "/api/wifi/status"): "h_wifi_status",
        ("GET", "/api/timezones"): "h_timezones",
    }
    ROUTES.update({("GET", p): "h_probe" for p in PROBE_PATHS})

    def h_probe(self):
        # Device redirects to its SoftAP IP; emulator targets itself
        port = self.server.server_address[1]
        self.send_response(302)
        self.send_header("Location", f"http://127.0.0.1:{port}/")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def h_index(self):
        self.serve_page_localized("portal/index.html")

    def h_css(self):
        self.serve_www("portal/portal.css")

    def h_js(self):
        self.serve_www("portal/portal.js")

    def h_scan(self):
        time.sleep(SCAN_DELAY_S)
        self.send_json(200, {"networks": SCAN_NETWORKS})

    def h_wifi_post(self):
        code, payload = self.emu.start_join(self.read_body())
        self.send_json(code, payload)

    def h_wifi_status(self):
        self.send_json(200, self.emu.wifi_status())


# ── CLI & startup ────────────────────────────────────────────────────────

def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Emulate the ESP32 weather station web servers locally "
                    "(management UI + captive portal) for fast page development.")
    parser.add_argument("--mgmt-port", type=int, default=8080,
                        help="management endpoint port (default 8080)")
    parser.add_argument("--portal-port", type=int, default=8081,
                        help="captive-portal endpoint port (default 8081)")
    parser.add_argument("--no-browser", action="store_true",
                        help="do not open the pages in the default browser")
    parser.add_argument("--history-hours", type=int, default=24,
                        help="hours of synthetic history at startup (default 24)")
    parser.add_argument("--join-outcome", choices=JOIN_OUTCOMES, default="success",
                        help="initial WiFi join outcome")
    parser.add_argument("--ota-outcome", choices=OTA_OUTCOMES, default="success",
                        help="initial firmware-update outcome")
    parser.add_argument("--sensor", choices=SENSOR_KINDS, default="bmp280",
                        help="sensor fitting detected at boot: bme280 (temp+"
                             "pressure+humidity), bmp280 (temp+pressure), "
                             "ds18b20 (temp only), none (default bmp280)")
    parser.add_argument("--sensor-invalid", action="store_true",
                        help="start with an invalid sensor reading")
    parser.add_argument("--time-not-synced", action="store_true",
                        help="start with time never synchronized")
    parser.add_argument("--wifi-disconnected", action="store_true",
                        help="start with WiFi in retrying state (mgmt status)")
    parser.add_argument("--storage-free-kb", type=int, default=1024,
                        help="reported free storage in kB (default 1024)")
    parser.add_argument("--bootlog", choices=BOOTLOG_STATES, default="present",
                        help="boot log availability (default present)")
    return parser.parse_args(argv)


def make_server(port, handler_cls, emu, label, flag):
    try:
        return EmuServer(("127.0.0.1", port), handler_cls, emu, label)
    except OSError as exc:
        sys.exit(f"error: cannot listen on 127.0.0.1:{port} ({exc.strerror or exc}).\n"
                 f"Is the port already in use? Pick another with {flag}.")


def main(argv=None):
    args = parse_args(argv)

    if not WWW_ROOT.is_dir():
        sys.exit(f"error: page assets not found at {WWW_ROOT}\n"
                 "The emulator serves components/web_server/www/ from this "
                 "repository — run it from a full checkout.")

    emu = Emulator(history_hours=args.history_hours,
                   join_outcome=args.join_outcome,
                   ota_outcome=args.ota_outcome,
                   sensor=args.sensor,
                   sensor_valid=not args.sensor_invalid,
                   time_synced=not args.time_not_synced,
                   wifi_connected=not args.wifi_disconnected,
                   storage_free_kb=args.storage_free_kb,
                   bootlog=args.bootlog)

    mgmt = make_server(args.mgmt_port, MgmtHandler, emu, "mgmt", "--mgmt-port")
    portal = make_server(args.portal_port, PortalHandler, emu, "portal",
                         "--portal-port")

    for server in (mgmt, portal):
        threading.Thread(target=server.serve_forever, daemon=True).start()

    mgmt_url = f"http://127.0.0.1:{args.mgmt_port}/"
    portal_url = f"http://127.0.0.1:{args.portal_port}/"
    print("ESP32 weather station emulator running (Ctrl-C to stop)")
    print(f"  management page : {mgmt_url}")
    print(f"  captive portal  : {portal_url}")
    print(f"  scenario control: {mgmt_url}emu")

    if not args.no_browser:
        webbrowser.open(mgmt_url)
        webbrowser.open(portal_url)

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nstopping…")
        mgmt.shutdown()
        portal.shutdown()


if __name__ == "__main__":
    main()
