"""Opt-in actual nginx HTTP policy and config-prefix regressions; no NAS access."""
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


@unittest.skipUnless(os.getenv("RUN_NGINX_ORIGIN_INTEGRATION") == "1",
                     "set RUN_NGINX_ORIGIN_INTEGRATION=1 for isolated nginx")
class NginxOriginTest(unittest.TestCase):
    def test_real_head_get_encoded_paths_and_spoofed_header_policy(self):
        spec = importlib.util.spec_from_file_location(
            "origin_guard", ROOT/"ops/install_ota_origin_guard.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix="smartbox-nginx-") as tmp:
            work = Path(tmp)
            name = "smartbox-nginx-" + uuid.uuid4().hex[:12]
            wrapper = work/"nginx-wrapper"
            args = ["docker", "run", "--rm", "--name", name, "--network", "none",
                    "--user", str(os.getuid())+":"+str(os.getgid()),
                    "-v", tmp+":"+tmp, "--entrypoint", "nginx", IMAGE]
            wrapper.write_text("#!/bin/sh\nexec "+shlex.join(args)+' "$@"\n')
            wrapper.chmod(0o700)
            module.NGINX = str(wrapper)
            try:
                count = module.fixture({"allowed_cidrs":
                    ["127.0.0.1/32", "::1/128", "192.168.10.0/24", "100.64.0.0/10"]}, work)
                self.assertEqual(28, count)
            finally:
                subprocess.run(["docker", "rm", "-f", name], capture_output=True, timeout=30)

    def test_relative_includes_need_original_main_config_directory(self):
        with tempfile.TemporaryDirectory(prefix="smartbox-nginx-prefix-") as tmp:
            path = Path(tmp)/"candidate.conf"
            path.write_text("events {}\nhttp { include mime.types; "
                            "server { listen 127.0.0.1:8080; return 204; } }\n")
            common = ["docker", "run", "--rm", "--network", "none",
                      "-v", tmp+":/fixtures:ro"]
            bad = subprocess.run([*common, "--entrypoint", "nginx", IMAGE,
                                  "-t", "-p", "/etc/nginx/", "-c", "/fixtures/candidate.conf"],
                                 capture_output=True, text=True, timeout=30)
            self.assertNotEqual(0, bad.returncode)
            self.assertIn("/fixtures/mime.types", bad.stderr)
            good = subprocess.run([*common, "--entrypoint", "sh", IMAGE, "-c",
                "cp /fixtures/candidate.conf /etc/nginx/.validation-test && "
                "nginx -t -p /etc/nginx/ -c /etc/nginx/.validation-test"],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(0, good.returncode, good.stderr)


if __name__ == "__main__":
    unittest.main()
