"""Manifest-only diagnostic failure, expiry and deployed acceptance contracts."""
import copy
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
IMAGE = "nginx@sha256:892bb2152464f0c5d8972593a5f5afaf8054552580b699d95f8ff4af08c2d33d"


def load():
    spec = importlib.util.spec_from_file_location("ota_fault", ROOT / "ops/ota_diagnostic_fault.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def telemetry(timestamp, attempts, state="UP_TO_DATE", code=200):
    return {"timestamp": timestamp, "fsm_state": "IDLE",
            "ota": {"state": state, "phase": "VERSION", "attempts": attempts,
                    "manifest_http_code": code, "updater_error": 0,
                    "request_pending": False}}


class DiagnosticFaultContractTest(unittest.TestCase):
    def test_production_peer_time_identity_and_auth_phase_preservation(self):
        m = load()
        http, location = m.render_fault("8.8.8.8", "ota-fixture", 100, 3)
        self.assertIn("geo $realip_remote_addr", http)
        self.assertIn("8.8.8.8/32 1;", http)
        self.assertIn("map $msec", http)
        self.assertIn("100|101|102", http)
        self.assertNotIn("103", http)
        self.assertIn("/version.json:1:ota-fixture:1", http)
        self.assertNotIn("$arg_", http)
        self.assertNotIn("forwarded", http.lower())
        self.assertNotIn("return", location)
        self.assertNotIn("auth_basic", location)
        self.assertIn("rewrite ^ /__smartbox_ota_diagnostic_", location)
        self.assertTrue(location.endswith(" break; }\n"))
        self.assertNotIn("/firmware.bin:", http)

    def test_input_and_window_bounds(self):
        m = load()
        for peer in ("8.8.8.0/24", "0.0.0.0", "224.0.0.1", "::1", "$http_x_real_ip"):
            with self.subTest(peer=peer), self.assertRaises(ValueError):
                m.render_fault(peer, "fixture", 100, 60)
        for username in ("", "x\"; return 200", "x/y"):
            with self.subTest(username=username), self.assertRaises(ValueError):
                m.render_fault("8.8.8.8", username, 100, 60)
        for start, seconds in ((True, 60), (100, True), (0, 60), (100, 0), (100, 121)):
            with self.subTest(start=start, seconds=seconds), self.assertRaises(ValueError):
                m.render_fault("8.8.8.8", "fixture", start, seconds)

    def test_actual_diagnostic_failure_and_no_update_recovery_contract(self):
        m = load()
        before, failed, recovered = telemetry(1000, 2), telemetry(2000, 3, "FAILED", 404), telemetry(3000, 4)
        result = m.acceptance(before, failed, recovered)
        self.assertTrue(result["manifest_failure_observed"])
        self.assertTrue(result["no_update_recovery_observed"])
        self.assertFalse(result["firmware_transfer_expected"])
        self.assertFalse(result["actuator_command_sent"])
        self.assertEqual({"timestamp", "fsm_state", "ota"}, set(m.diagnostic_snapshot(
            {**before, "arbitrary_private_field": "discard"})))

    def test_stale_reboot_queued_or_unbounded_diagnostics_refused(self):
        m = load()
        before, failed, recovered = telemetry(1000, 2), telemetry(2000, 3, "FAILED", 404), telemetry(3000, 4)
        invalid = []
        for key, value in (("request_pending", True), ("attempts", True),
                           ("phase", "FIRMWARE"), ("manifest_http_code", 200),
                           ("url", "do-not-serialize")):
            item = copy.deepcopy(failed)
            item["ota"][key] = value
            invalid.append(item)
        item = copy.deepcopy(failed);item["timestamp"] = 500;invalid.append(item)
        item = copy.deepcopy(failed);item["fsm_state"] = "OPENING";invalid.append(item)
        for item in invalid:
            with self.subTest(item=item), self.assertRaises(ValueError):
                m.acceptance(before, item, recovered)
        with self.assertRaises(ValueError):
            m.acceptance(before, failed, telemetry(3000, 6))


@unittest.skipUnless(os.getenv("RUN_NGINX_ORIGIN_INTEGRATION") == "1",
                     "isolated real-nginx diagnostic fault opt-in")
class ActualNginxDiagnosticFaultTest(unittest.TestCase):
    def test_authentication_expiry_paths_and_mutations(self):
        r = subprocess.run(["docker", "pull", IMAGE], capture_output=True, timeout=120)
        if r.returncode:
            raise RuntimeError("pinned nginx fixture image unavailable")
        with tempfile.TemporaryDirectory(prefix="smartbox-diag-nginx-") as directory:
            root = Path(directory)
            name = "smartbox-diag-" + uuid.uuid4().hex[:12]
            wrapper = root / "nginx-wrapper"
            args = ["docker", "run", "--rm", "--name", name, "--network", "none",
                    "--user", str(os.getuid()) + ":" + str(os.getgid()),
                    "-v", directory + ":" + directory, "--entrypoint", "nginx", IMAGE]
            wrapper.write_text("#!/bin/sh\nexec " + shlex.join(args) + ' "$@"\n')
            wrapper.chmod(0o700)
            try:
                self.assertEqual(52, load().fixture(root, nginx=str(wrapper)))
            finally:
                subprocess.run(["docker", "rm", "-f", name], capture_output=True, timeout=30)
