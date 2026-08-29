"""Unit tests for the hardware emulator's pure logic.

Run from the repository root:
    python -m unittest tools.test_hw_emulator -v
or directly:
    python tools/test_hw_emulator.py
"""

import http.client
import json
import os
import sys
import threading
import unittest

sys.path.insert(0, os.path.dirname(__file__))

from hw_emulator import (  # noqa: E402
    HISTORY_INTERVAL_S,
    JOIN_RESOLVE_DELAY_S,
    OTA_MAX_SIZE,
    OTA_REBOOT_DELAY_S,
    SUPPORTED_LANGS,
    TIMEZONES,
    EmuServer,
    Emulator,
    MgmtHandler,
    PortalHandler,
    accept_language_pick,
    humidity_at,
    pressure_at,
    temperature_at,
)

T0 = 1_783_939_200  # fixed epoch for deterministic tests


def make_emu(**kwargs):
    kwargs.setdefault("now", T0)
    return Emulator(**kwargs)


class AcceptLanguagePickTests(unittest.TestCase):
    """Mirrors components/web_server/test/test_i18n.c."""

    def test_simple_de(self):
        self.assertEqual(accept_language_pick("de"), "de")

    def test_fr_subtag(self):
        self.assertEqual(accept_language_pick("fr-FR,fr;q=0.9,en;q=0.8"), "fr")

    def test_unknown_falls_back_to_en(self):
        self.assertEqual(accept_language_pick("zh-TW"), "en")

    def test_uk_preferred_via_q_value(self):
        self.assertEqual(accept_language_pick("uk,en;q=0.5"), "uk")

    def test_empty_header(self):
        self.assertEqual(accept_language_pick(""), "en")

    def test_none_header(self):
        self.assertEqual(accept_language_pick(None), "en")

    def test_de_at_primary_subtag(self):
        self.assertEqual(accept_language_pick("de-AT"), "de")

    def test_higher_q_wins_regardless_of_order(self):
        self.assertEqual(accept_language_pick("en;q=0.3,de;q=0.9"), "de")


class TemperatureTests(unittest.TestCase):
    def test_deterministic(self):
        self.assertEqual(temperature_at(T0), temperature_at(T0))

    def test_within_range(self):
        for ts in range(T0, T0 + 86400, 600):
            self.assertGreaterEqual(temperature_at(ts), 16.5)
            self.assertLessEqual(temperature_at(ts), 25.5)

    def test_varies_over_hours(self):
        values = {round(temperature_at(T0 + h * 3600), 2) for h in range(12)}
        self.assertGreater(len(values), 1)


class HistoryTests(unittest.TestCase):
    def test_count_matches_hours(self):
        emu = make_emu(history_hours=24)
        self.assertEqual(emu.history_count(now=T0), 24 * 60 + 1)

    def test_records_ascending_and_spaced(self):
        emu = make_emu(history_hours=1)
        records = emu.history_records(now=T0)
        timestamps = [ts for ts, _ in records]
        self.assertEqual(timestamps, sorted(timestamps))
        deltas = {b - a for a, b in zip(timestamps, timestamps[1:])}
        self.assertEqual(deltas, {HISTORY_INTERVAL_S})

    def test_from_to_filter_inclusive(self):
        emu = make_emu(history_hours=2)
        lo = T0 - 1800
        hi = T0 - 600
        records = emu.history_records(from_ts=lo, to_ts=hi, now=T0)
        self.assertTrue(all(lo <= ts <= hi for ts, _ in records))
        self.assertEqual(len(records), (hi - lo) // HISTORY_INTERVAL_S + 1)

    def test_grows_as_time_passes(self):
        emu = make_emu(history_hours=1)
        before = emu.history_count(now=T0)
        after = emu.history_count(now=T0 + 600)
        self.assertEqual(after, before + 10)


class ConfigTests(unittest.TestCase):
    def test_valid_full_update(self):
        emu = make_emu()
        self.assertIsNone(emu.apply_config(
            {"tz_name": "Europe/Kyiv", "time_mode": "utc", "temp_unit": "F"}))
        status = emu.status(now=T0)
        self.assertEqual(status["tz_name"], "Europe/Kyiv")
        self.assertEqual(status["time_mode"], "utc")
        self.assertEqual(status["temp_unit"], "F")

    def test_partial_update(self):
        emu = make_emu()
        self.assertIsNone(emu.apply_config({"temp_unit": "F"}))
        status = emu.status(now=T0)
        self.assertEqual(status["temp_unit"], "F")
        self.assertEqual(status["tz_name"], "UTC")

    def test_unknown_timezone(self):
        emu = make_emu()
        self.assertEqual(emu.apply_config({"tz_name": "Not/AZone"}),
                         "unknown_timezone")

    def test_invalid_time_mode(self):
        self.assertEqual(make_emu().apply_config({"time_mode": "solar"}),
                         "invalid_time_mode")

    def test_invalid_temp_unit(self):
        self.assertEqual(make_emu().apply_config({"temp_unit": "K"}),
                         "invalid_temp_unit")

    def test_error_stops_processing_but_keeps_earlier_fields(self):
        # Firmware applies fields in order and aborts on the first bad one
        emu = make_emu()
        error = emu.apply_config({"tz_name": "Europe/Berlin", "time_mode": "bad"})
        self.assertEqual(error, "invalid_time_mode")
        self.assertEqual(emu.status(now=T0)["tz_name"], "Europe/Berlin")


class JoinSessionTests(unittest.TestCase):
    @staticmethod
    def body(**kwargs):
        return json.dumps(kwargs).encode()

    def test_empty_body(self):
        code, payload = make_emu().start_join(b"", now=T0)
        self.assertEqual((code, payload["error"]), (400, "empty_body"))

    def test_invalid_ssid_missing_and_too_long(self):
        emu = make_emu()
        code, payload = emu.start_join(self.body(password="x"), now=T0)
        self.assertEqual((code, payload["error"]), (400, "invalid_ssid"))
        code, payload = emu.start_join(self.body(ssid="s" * 33), now=T0)
        self.assertEqual((code, payload["error"]), (400, "invalid_ssid"))

    def test_password_too_long(self):
        code, payload = make_emu().start_join(
            self.body(ssid="HomeNet", password="p" * 64), now=T0)
        self.assertEqual((code, payload["error"]), (400, "password_too_long"))

    def test_success_flow(self):
        emu = make_emu()
        code, payload = emu.start_join(self.body(ssid="HomeNet", password="pw"),
                                       now=T0)
        self.assertEqual((code, payload["status"]), (202, "connecting"))
        self.assertEqual(emu.wifi_status(now=T0 + 1),
                         {"state": "connecting", "ip": None, "reason": None})
        status = emu.wifi_status(now=T0 + JOIN_RESOLVE_DELAY_S + 0.1)
        self.assertEqual(status["state"], "connected")
        self.assertEqual(status["suffix"], emu.device_suffix)
        self.assertIsNone(status["reason"])

    def test_scenario_auth_failure(self):
        emu = make_emu(join_outcome="auth")
        emu.start_join(self.body(ssid="HomeNet", password="bad"), now=T0)
        status = emu.wifi_status(now=T0 + JOIN_RESOLVE_DELAY_S + 0.1)
        self.assertEqual((status["state"], status["reason"]), ("failed", "auth"))

    def test_designated_ssid_overrides_scenario(self):
        emu = make_emu(join_outcome="success")
        emu.start_join(self.body(ssid="Emu-NotFound", password="pw"), now=T0)
        status = emu.wifi_status(now=T0 + JOIN_RESOLVE_DELAY_S + 0.1)
        self.assertEqual((status["state"], status["reason"]),
                         ("failed", "not_found"))

    def test_reattempt_restarts_session(self):
        emu = make_emu(join_outcome="auth")
        emu.start_join(self.body(ssid="HomeNet"), now=T0)
        emu.wifi_status(now=T0 + JOIN_RESOLVE_DELAY_S + 0.1)  # resolves failed
        emu.start_join(self.body(ssid="HomeNet"), now=T0 + 10)
        self.assertEqual(emu.wifi_status(now=T0 + 10.5)["state"], "connecting")

    def test_valid_tz_applied_invalid_ignored(self):
        emu = make_emu()
        emu.start_join(self.body(ssid="HomeNet", tz_name="Europe/Kyiv"), now=T0)
        self.assertEqual(emu.tz_name, "Europe/Kyiv")
        emu.start_join(self.body(ssid="HomeNet", tz_name="Bad/Zone"), now=T0)
        self.assertEqual(emu.tz_name, "Europe/Kyiv")


class OtaSessionTests(unittest.TestCase):
    def test_size_limits(self):
        emu = make_emu()
        code, payload = emu.ota_begin(0)
        self.assertEqual((code, payload["error"]), (507, "image_too_large"))
        code, payload = emu.ota_begin(OTA_MAX_SIZE + 1)
        self.assertEqual((code, payload["error"]), (507, "image_too_large"))

    def test_409_while_receiving(self):
        emu = make_emu()
        self.assertIsNone(emu.ota_begin(1000))
        code, payload = emu.ota_begin(1000)
        self.assertEqual((code, payload["error"]), (409, "update_in_progress"))

    def test_progress_tracks_bytes(self):
        emu = make_emu()
        emu.ota_begin(1000)
        emu.ota_progress_update(500, 1000)
        self.assertEqual(emu.ota_status(now=T0)["progress_pct"], 50)

    def test_success_path_and_simulated_reboot(self):
        emu = make_emu()
        emu.ota_begin(1000)
        emu.ota_progress_update(1000, 1000)
        emu.ota_validating()
        code, payload = emu.ota_finish(now=T0)
        self.assertEqual(code, 200)
        self.assertEqual(payload, {"status": "applied", "reboot_in_s": 3})
        self.assertEqual(emu.ota_status(now=T0 + 1)["state"],
                         "applied_pending_reboot")
        # After the reboot delay: state idle, uptime reset, version bumped
        reboot_time = T0 + OTA_REBOOT_DELAY_S + 0.5
        self.assertEqual(emu.ota_status(now=reboot_time)["state"], "idle")
        status = emu.status(now=reboot_time + 2)
        self.assertLessEqual(status["uptime_s"], 3)
        self.assertEqual(status["fw_version"], "emu-0.1.1")

    def test_invalid_image_outcome(self):
        emu = make_emu(ota_outcome="invalid_image")
        emu.ota_begin(1000)
        emu.ota_validating()
        code, payload = emu.ota_finish(now=T0)
        self.assertEqual(code, 400)
        self.assertEqual(payload["error"], "invalid_image")
        self.assertEqual(emu.ota_status(now=T0)["error"], "invalid_image")

    def test_write_error_outcome(self):
        emu = make_emu(ota_outcome="write_error")
        emu.ota_begin(1000)
        emu.ota_validating()
        code, _ = emu.ota_finish(now=T0)
        self.assertEqual(code, 500)
        self.assertEqual(emu.ota_status(now=T0)["state"], "failed")

    def test_retry_allowed_after_failure(self):
        emu = make_emu(ota_outcome="write_error")
        emu.ota_begin(1000)
        emu.ota_validating()
        emu.ota_finish(now=T0)
        self.assertIsNone(emu.ota_begin(1000))


class ScenarioTests(unittest.TestCase):
    def test_valid_patch(self):
        emu = make_emu()
        self.assertIsNone(emu.scenario_patch(
            {"join_outcome": "auth", "sensor_valid": False}))
        snapshot = emu.scenario_snapshot()
        self.assertEqual(snapshot["join_outcome"], "auth")
        self.assertFalse(snapshot["sensor_valid"])

    def test_unknown_field_rejected(self):
        self.assertEqual(make_emu().scenario_patch({"bogus": 1}), "bogus")

    def test_invalid_value_rejected(self):
        emu = make_emu()
        self.assertEqual(emu.scenario_patch({"join_outcome": "maybe"}),
                         "join_outcome")
        self.assertEqual(emu.scenario_patch({"sensor_valid": "yes"}),
                         "sensor_valid")
        self.assertEqual(emu.scenario_patch({"storage_free_kb": -1}),
                         "storage_free_kb")
        self.assertEqual(emu.scenario_patch({"storage_free_kb": True}),
                         "storage_free_kb")

    def test_non_dict_body_rejected(self):
        self.assertEqual(make_emu().scenario_patch(None), "(body)")

    def test_effects_on_status(self):
        emu = make_emu(sensor_valid=False, time_synced=False,
                       wifi_connected=False, storage_free_kb=7)
        status = emu.status(now=T0)
        self.assertFalse(status["temperature_valid"])
        self.assertFalse(status["time_synced"])
        self.assertEqual(status["time_source"], "none")
        self.assertIsNone(status["time_last_sync"])
        self.assertEqual(status["wifi"],
                         {"state": "retrying", "ssid": "", "rssi": 0, "ip": ""})
        self.assertEqual(status["storage_free_kb"], 7)

    def test_status_defaults(self):
        status = make_emu().status(now=T0)
        self.assertTrue(status["temperature_valid"])
        self.assertEqual(status["time_source"], "ntp")
        self.assertEqual(status["wifi"]["state"], "connected")
        self.assertEqual(status["fw_version"], "emu-0.1.0")
        self.assertIn(status["tz_name"], TIMEZONES)


class PressureSimTests(unittest.TestCase):
    """Spec 005: deterministic pressure simulation."""

    def test_deterministic(self):
        self.assertEqual(pressure_at(T0), pressure_at(T0))

    def test_within_plausible_range(self):
        for ts in range(T0, T0 + 2 * 86400, 1800):
            self.assertGreaterEqual(pressure_at(ts), 1004.0)
            self.assertLessEqual(pressure_at(ts), 1022.0)

    def test_varies_over_hours(self):
        values = {round(pressure_at(T0 + h * 3600), 1) for h in range(24)}
        self.assertGreater(len(values), 1)


class HumiditySimTests(unittest.TestCase):
    """Spec 007: deterministic humidity simulation (research D7)."""

    def test_deterministic(self):
        self.assertEqual(humidity_at(T0), humidity_at(T0))

    def test_within_range_0_100(self):
        for ts in range(T0, T0 + 2 * 86400, 900):
            self.assertGreaterEqual(humidity_at(ts), 0.0)
            self.assertLessEqual(humidity_at(ts), 100.0)

    def test_varies_over_hours(self):
        values = {round(humidity_at(T0 + h * 3600), 1) for h in range(24)}
        self.assertGreater(len(values), 1)


class SensorScenarioTests(unittest.TestCase):
    """Spec 005/007 contract §1/§5: status fields per fitting; SC-003 regression."""

    def test_status_default_bmp280(self):
        status = make_emu().status(now=T0)
        self.assertEqual(status["sensor"], "bmp280")
        self.assertTrue(status["pressure_valid"])
        self.assertTrue(status["temperature_valid"])
        self.assertFalse(status["humidity_valid"])
        self.assertAlmostEqual(status["pressure_hpa"],
                               round(pressure_at(T0), 1))

    def test_status_bme280_has_all_three(self):
        status = make_emu(sensor="bme280").status(now=T0)
        self.assertEqual(status["sensor"], "bme280")
        self.assertTrue(status["temperature_valid"])
        self.assertTrue(status["pressure_valid"])
        self.assertTrue(status["humidity_valid"])
        self.assertAlmostEqual(status["humidity_pct"],
                               round(humidity_at(T0), 1))

    def test_status_ds18b20_probe_only(self):
        status = make_emu(sensor="ds18b20").status(now=T0)
        self.assertEqual(status["sensor"], "ds18b20")
        self.assertFalse(status["pressure_valid"])
        self.assertFalse(status["humidity_valid"])
        self.assertTrue(status["temperature_valid"])  # probe still works

    def test_status_no_sensor(self):
        status = make_emu(sensor="none").status(now=T0)
        self.assertEqual(status["sensor"], "none")
        self.assertFalse(status["pressure_valid"])
        self.assertFalse(status["humidity_valid"])
        self.assertFalse(status["temperature_valid"])

    def test_humidity_fields_always_present(self):
        for kind in ("bme280", "bmp280", "ds18b20", "none"):
            status = make_emu(sensor=kind).status(now=T0)
            self.assertIn("humidity_pct", status)
            self.assertIn("humidity_valid", status)
            self.assertIsInstance(status["humidity_valid"], bool)

    def test_bme280_invalid_reading_invalidates_all_three(self):
        status = make_emu(sensor="bme280", sensor_valid=False).status(now=T0)
        self.assertFalse(status["temperature_valid"])
        self.assertFalse(status["pressure_valid"])
        self.assertFalse(status["humidity_valid"])

    def test_non_bme280_never_reports_humidity_valid(self):
        """SC-003: no humidity artefacts on non-BME280 fittings."""
        for kind in ("bmp280", "ds18b20", "none"):
            self.assertFalse(make_emu(sensor=kind).status(now=T0)["humidity_valid"])

    def test_bmp280_invalid_reading_invalidates_both(self):
        status = make_emu(sensor="bmp280", sensor_valid=False).status(now=T0)
        self.assertFalse(status["temperature_valid"])
        self.assertFalse(status["pressure_valid"])

    def test_scenario_patch_switches_fitting(self):
        emu = make_emu()
        self.assertIsNone(emu.scenario_patch({"sensor": "ds18b20"}, now=T0))
        self.assertFalse(emu.status(now=T0 + 1)["pressure_valid"])
        self.assertIsNone(emu.scenario_patch({"sensor": "bmp280"}, now=T0 + 10))
        self.assertTrue(emu.status(now=T0 + 11)["pressure_valid"])

    def test_scenario_rejects_unknown_sensor(self):
        self.assertEqual(make_emu().scenario_patch({"sensor": "bme680"}),
                         "sensor")

    def test_history_pressure_follows_fitting_segments(self):
        emu = make_emu()  # bmp280 since epoch 0
        self.assertIsNotNone(emu.history_pressure_at(T0 - 3600))
        emu.scenario_patch({"sensor": "ds18b20"}, now=T0)
        emu.scenario_patch({"sensor": "bmp280"}, now=T0 + 600)
        self.assertIsNotNone(emu.history_pressure_at(T0 - 60))   # before switch
        self.assertIsNone(emu.history_pressure_at(T0 + 60))      # probe window
        self.assertIsNotNone(emu.history_pressure_at(T0 + 660))  # after switch


class _QuietMixin:
    """Silence per-request log lines during tests."""

    def log_message(self, fmt, *args):
        pass


class QuietPortalHandler(_QuietMixin, PortalHandler):
    pass


class QuietMgmtHandler(_QuietMixin, MgmtHandler):
    pass


# Contract vectors: specs/004-fix-web-i18n/contracts/i18n-http.md §1 (SC-004).
# None means "no Accept-Language header sent at all".
I18N_PAGE_VECTORS = [
    ("de", "de"),
    ("de-AT,en;q=0.8", "de"),
    ("fr-FR,fr;q=0.9,en;q=0.8", "fr"),
    ("uk,en;q=0.5", "uk"),
    ("ja, zh;q=0.9, uk;q=0.2", "uk"),
    ("zh-TW", "en"),
    ("", "en"),
    (None, "en"),
    # >255 chars with a supported language inside the first 255: still found.
    ("de," + "x" * 300, "de"),
    # Supported language only after the 255-char cut: truncated away → en.
    ("xx," * 90 + "de", "en"),
]


class _I18nHttpBase(unittest.TestCase):
    """Spin up one emulator endpoint and issue real HTTP requests."""

    handler_cls = None  # set by subclasses

    @classmethod
    def setUpClass(cls):
        cls.emu = make_emu(now=None)
        cls.server = EmuServer(("127.0.0.1", 0), cls.handler_cls, cls.emu,
                               "test")
        cls.port = cls.server.server_address[1]
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def http_get(self, path, headers=None):
        """Raw GET returning (response, body). http.client keeps the path
        as given (urllib would normalize it)."""
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        try:
            conn.putrequest("GET", path)
            for key, value in (headers or {}).items():
                conn.putheader(key, value)
            conn.endheaders()
            resp = conn.getresponse()
            return resp, resp.read()
        finally:
            conn.close()


class I18nPageContractMixin:
    """Shared assertions for GET / — run against both endpoints
    (contracts/i18n-http.md applies to portal and management alike)."""

    def test_lang_vectors_patch_attribute_and_header(self):
        for header, expected in I18N_PAGE_VECTORS:
            with self.subTest(accept_language=header):
                headers = {} if header is None else {"Accept-Language": header}
                resp, body = self.http_get("/", headers)
                self.assertEqual(resp.status, 200)
                self.assertEqual(resp.getheader("Content-Language"), expected)
                self.assertIn(f'<html lang="{expected}"'.encode(), body)
                # Exactly one language declaration — original 'en' replaced.
                self.assertEqual(body.count(b"<html"), 1)

    def test_all_packs_served_as_json(self):
        for code in SUPPORTED_LANGS:
            with self.subTest(lang=code):
                resp, body = self.http_get(f"/i18n/{code}.json")
                self.assertEqual(resp.status, 200)
                self.assertIn("application/json",
                              resp.getheader("Content-Type", ""))
                pack = json.loads(body)
                self.assertIsInstance(pack, dict)
                self.assertTrue(pack)

    def test_unsupported_pack_paths_404(self):
        for path in ("/i18n/zz.json", "/i18n/es.json", "/i18n/en.json.bak",
                     "/i18n/", "/i18n/EN.json"):
            with self.subTest(path=path):
                resp, body = self.http_get(path)
                self.assertEqual(resp.status, 404)
                self.assertNotIn(path.encode(), body)  # no path echo

    def test_shared_applier_served(self):
        resp, body = self.http_get("/i18n.js")
        self.assertEqual(resp.status, 200)
        self.assertIn("javascript", resp.getheader("Content-Type", ""))
        self.assertIn(b"i18nReady", body)


class PortalI18nHttpTests(I18nPageContractMixin, _I18nHttpBase):
    handler_cls = QuietPortalHandler


class MgmtI18nHttpTests(I18nPageContractMixin, _I18nHttpBase):
    handler_cls = QuietMgmtHandler


class I18nInventoryTests(unittest.TestCase):
    """SC-006: the four language packs and both pages stay consistent."""

    def test_checker_reports_zero_discrepancies(self):
        import check_i18n
        self.assertEqual(check_i18n.main(), 0)


class PressureHttpTests(_I18nHttpBase):
    """Spec 005 contract §1–§3 over real HTTP on the management endpoint."""

    handler_cls = QuietMgmtHandler

    def test_status_has_pressure_fields(self):
        resp, body = self.http_get("/api/status")
        self.assertEqual(resp.status, 200)
        status = json.loads(body)
        for key in ("pressure_hpa", "pressure_valid", "sensor"):
            self.assertIn(key, status)
        self.assertIn(status["sensor"], ("bme280", "bmp280", "ds18b20", "none"))

    def test_history_records_always_carry_pressure_key(self):
        resp, body = self.http_get("/api/history")
        self.assertEqual(resp.status, 200)
        records = json.loads(body)["records"]
        self.assertTrue(records)
        for r in records[:50]:
            self.assertIn("pressure", r)
            self.assertTrue(r["pressure"] is None
                            or isinstance(r["pressure"], (int, float)))
            if r["pressure"] is not None:
                self.assertNotEqual(r["pressure"], 0)

    def test_history_pressure_null_after_fitting_switch(self):
        self.emu.scenario_patch({"sensor": "ds18b20"}, now=0)
        try:
            _, body = self.http_get("/api/history")
            records = json.loads(body)["records"]
            self.assertTrue(all(r["pressure"] is None for r in records))
        finally:
            self.emu.scenario_patch({"sensor": "bmp280"}, now=0)
            self.emu.sensor_segments = [(0, "bmp280")]

    def test_csv_columns(self):
        resp, body = self.http_get("/api/history.csv")
        self.assertEqual(resp.status, 200)
        lines = body.decode().strip().splitlines()
        self.assertEqual(
            lines[0], "timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct")
        for line in lines[1:20]:
            parts = line.split(",")
            self.assertEqual(len(parts), 4)
            float(parts[1])                       # temperature parses
            if parts[2]:
                self.assertGreater(float(parts[2]), 300.0)

    def test_csv_blank_pressure_when_probe_only(self):
        self.emu.scenario_patch({"sensor": "ds18b20"}, now=0)
        try:
            _, body = self.http_get("/api/history.csv")
            lines = body.decode().strip().splitlines()
            # probe-only: both pressure and humidity cells empty → trailing ",,"
            self.assertTrue(all(line.endswith(",,") for line in lines[1:]))
        finally:
            self.emu.scenario_patch({"sensor": "bmp280"}, now=0)
            self.emu.sensor_segments = [(0, "bmp280")]


class HumidityHttpTests(_I18nHttpBase):
    """Spec 007 contract §1–§3 over real HTTP on the management endpoint."""

    handler_cls = QuietMgmtHandler

    def setUp(self):
        super().setUp()
        self.emu.scenario_patch({"sensor": "bme280"}, now=0)
        self.emu.sensor_segments = [(0, "bme280")]

    def test_status_has_humidity_fields(self):
        _, body = self.http_get("/api/status")
        status = json.loads(body)
        for key in ("humidity_pct", "humidity_valid"):
            self.assertIn(key, status)
        self.assertTrue(status["humidity_valid"])
        self.assertTrue(status["pressure_valid"])   # BME280 supplies pressure too

    def test_history_records_always_carry_humidity_key(self):
        _, body = self.http_get("/api/history")
        records = json.loads(body)["records"]
        self.assertTrue(records)
        for r in records[:50]:
            self.assertIn("humidity", r)
            self.assertTrue(r["humidity"] is None
                            or isinstance(r["humidity"], (int, float)))
            if r["humidity"] is not None:
                self.assertGreaterEqual(r["humidity"], 0.0)
                self.assertLessEqual(r["humidity"], 100.0)

    def test_history_humidity_null_after_switch_to_bmp280(self):
        self.emu.scenario_patch({"sensor": "bmp280"}, now=0)
        self.emu.sensor_segments = [(0, "bmp280")]
        _, body = self.http_get("/api/history")
        records = json.loads(body)["records"]
        self.assertTrue(all(r["humidity"] is None for r in records))
        # pressure still present for the BMP280 window (regression guard)
        self.assertTrue(any(r["pressure"] is not None for r in records))

    def test_history_humidity_follows_fitting_segments(self):
        self.emu.scenario_patch({"sensor": "bmp280"}, now=T0)
        self.emu.scenario_patch({"sensor": "bme280"}, now=T0 + 600)
        self.assertIsNotNone(self.emu.history_humidity_at(T0 - 60))   # bme280 window
        self.assertIsNone(self.emu.history_humidity_at(T0 + 60))      # bmp280 window
        self.assertIsNotNone(self.emu.history_humidity_at(T0 + 660))  # bme280 again

    def test_csv_humidity_column(self):
        _, body = self.http_get("/api/history.csv")
        lines = body.decode().strip().splitlines()
        self.assertEqual(
            lines[0], "timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct")
        for line in lines[1:20]:
            parts = line.split(",")
            self.assertEqual(len(parts), 4)
            self.assertTrue(parts[3])              # humidity cell populated
            self.assertGreaterEqual(float(parts[3]), 0.0)
            self.assertLessEqual(float(parts[3]), 100.0)

    def test_bootlog_bme280_mentions_humidity(self):
        _, body = self.http_get("/api/boot.log")
        text = body.decode()
        self.assertIn("BME280", text)
        self.assertIn("humidity", text)


class BootLogScenarioTests(unittest.TestCase):
    """Spec 006: bootlog scenario field validation and synthetic content."""

    def test_default_present(self):
        self.assertEqual(make_emu().scenario_snapshot()["bootlog"], "present")

    def test_ctor_missing(self):
        emu = make_emu(bootlog="missing")
        self.assertEqual(emu.scenario_snapshot()["bootlog"], "missing")
        self.assertIsNone(emu.bootlog_text())

    def test_patch_validates_values(self):
        emu = make_emu()
        self.assertIsNone(emu.scenario_patch({"bootlog": "missing"}))
        self.assertEqual(emu.scenario_patch({"bootlog": "gone"}), "bootlog")

    def test_content_tracks_sensor_kind(self):
        emu = make_emu()
        expectations = {
            "bmp280": ("BMP280 found at 0x76",),
            "ds18b20": ("BMP280 not found at 0x76 or 0x77", "DS18B20 found"),
            "none": ("BMP280 not found at 0x76 or 0x77",
                     "No DS18B20 device found on bus"),
        }
        for kind, needles in expectations.items():
            with self.subTest(sensor=kind):
                emu.scenario_patch({"sensor": kind}, now=T0)
                text = emu.bootlog_text()
                for needle in needles:
                    self.assertIn(needle, text)
                self.assertIn("display: UI initialized", text)
                self.assertLessEqual(len(text.encode()), 4096)


class BootLogHttpTests(_I18nHttpBase):
    """Spec 006 contracts/bootlog-api.md over real HTTP (US1/US2)."""

    handler_cls = QuietMgmtHandler

    def tearDown(self):
        self.emu.scenario_patch({"bootlog": "present", "sensor": "bmp280"},
                                now=0)
        self.emu.sensor_segments = [(0, "bmp280")]

    def test_200_text_plain_within_cap(self):
        resp, body = self.http_get("/api/boot.log")
        self.assertEqual(resp.status, 200)
        self.assertIn("text/plain", resp.getheader("Content-Type", ""))
        self.assertLessEqual(len(body), 4096)
        self.assertIn(b"sensor: Sensor mode:", body)
        self.assertIn(b"display: UI initialized", body)

    def test_content_consistent_with_each_sensor_kind(self):
        for kind, needle in (
            ("bmp280", b"BMP280 found at 0x76"),
            ("ds18b20", b"DS18B20 found"),
            ("none", b"No DS18B20 device found on bus"),
        ):
            with self.subTest(sensor=kind):
                self.emu.scenario_patch({"sensor": kind}, now=0)
                resp, body = self.http_get("/api/boot.log")
                self.assertEqual(resp.status, 200)
                self.assertIn(needle, body)

    def test_missing_scenario_returns_404_shape(self):
        self.emu.scenario_patch({"bootlog": "missing"})
        resp, body = self.http_get("/api/boot.log")
        self.assertEqual(resp.status, 404)
        self.assertIn("application/json", resp.getheader("Content-Type", ""))
        self.assertEqual(json.loads(body), {"error": "not_found"})

    def test_view_and_download_byte_identical(self):
        # US2 scenario 2: the same endpoint feeds fetch() and the download
        # link, so two consecutive responses must match byte for byte.
        _, first = self.http_get("/api/boot.log")
        _, second = self.http_get("/api/boot.log")
        self.assertEqual(first, second)

    def test_mgmt_page_has_bootlog_section(self):
        resp, body = self.http_get("/")
        self.assertEqual(resp.status, 200)
        self.assertIn(b'id="bootlog-content"', body)
        self.assertIn(b'data-i18n="mgmt_heading_bootlog"', body)
        self.assertIn(b'id="btn-bootlog"', body)
        self.assertIn(b'download="boot.log"', body)
        self.assertIn(b'data-i18n="mgmt_bootlog_unavailable"', body)

    def test_i18n_packs_have_bootlog_keys(self):
        keys = ("mgmt_heading_bootlog", "mgmt_btn_bootlog",
                "mgmt_bootlog_unavailable")
        for code in SUPPORTED_LANGS:
            with self.subTest(lang=code):
                _, body = self.http_get(f"/i18n/{code}.json")
                pack = json.loads(body)
                for key in keys:
                    self.assertIn(key, pack)
                    self.assertTrue(pack[key].strip())


class BrokenI18nPortalHandler(QuietPortalHandler):
    SERVE_I18N = False


class I18nFaultToleranceTests(_I18nHttpBase):
    """SC-005 / User Story 3, server side: with pack delivery broken the
    page itself must still be served (language-patched); the browser-side
    English fallback is validated per quickstart.md §3."""

    handler_cls = BrokenI18nPortalHandler

    def test_page_still_served_when_packs_unavailable(self):
        resp, body = self.http_get("/", {"Accept-Language": "de"})
        self.assertEqual(resp.status, 200)
        self.assertIn(b'<html lang="de"', body)

    def test_packs_and_applier_unavailable(self):
        for path in ("/i18n/de.json", "/i18n/en.json", "/i18n.js"):
            with self.subTest(path=path):
                resp, _ = self.http_get(path)
                self.assertEqual(resp.status, 404)


if __name__ == "__main__":
    unittest.main()
