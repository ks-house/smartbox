"""Opt-in actual nginx HTTP policy and config-prefix regressions; no NAS access."""
import importlib.util
import http.client
import json
import socket
import sys
import time
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import uuid

ROOT = Path(__file__).resolve().parents[1]
IMAGE = "nginx@sha256:892bb2152464f0c5d8972593a5f5afaf8054552580b699d95f8ff4af08c2d33d"


@unittest.skipUnless(os.getenv("RUN_NGINX_ORIGIN_INTEGRATION") == "1",
                     "set RUN_NGINX_ORIGIN_INTEGRATION=1 for isolated nginx")
class NginxOriginTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Pull completion is separate from nginx's bounded startup deadline.
        result = subprocess.run(["docker", "pull", IMAGE], capture_output=True,
                                text=True, timeout=120)
        if result.returncode:
            raise RuntimeError("pinned nginx fixture download failed: " + result.stderr[-1000:])

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

    def test_additional_device_lan_preserves_public_refusal(self):
        spec = importlib.util.spec_from_file_location(
            "origin_guard", ROOT/"ops/install_ota_origin_guard.py")
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix="smartbox-nginx-lan-") as tmp:
            work = Path(tmp);name = "smartbox-nginx-" + uuid.uuid4().hex[:12]
            wrapper = work/"nginx-wrapper"
            args = ["docker","run","--rm","--name",name,"--network","none",
                    "--user",str(os.getuid())+":"+str(os.getgid()),"-v",tmp+":"+tmp,
                    "--entrypoint","nginx",IMAGE]
            wrapper.write_text("#!/bin/sh\nexec "+shlex.join(args)+' "$@"\n')
            wrapper.chmod(0o700);module.NGINX=str(wrapper)
            try:
                self.assertEqual(30,module.fixture({"allowed_cidrs":
                    ["127.0.0.1/32","::1/128","192.168.10.0/24",
                     "100.64.0.0/10","192.168.23.0/24"]},work))
            finally:
                subprocess.run(["docker","rm","-f",name],capture_output=True,timeout=30)

    def test_conditional_trace_records_real_status_and_omits_other_paths_queries(self):
        for module_name in ("install_ota_origin_guard","update_ota_origin_guard","observe_ota_origin"):
            spec=importlib.util.spec_from_file_location(module_name,ROOT/"ops"/(module_name+".py"))
            module=importlib.util.module_from_spec(spec);sys.modules[module_name]=module;spec.loader.exec_module(module)
        trace=module
        with tempfile.TemporaryDirectory(prefix="smartbox-trace-") as tmp:
            work=Path(tmp);name="smartbox-trace-"+uuid.uuid4().hex[:12]
            sock=work/"http.sock";log=work/"requests.jsonl"
            config=("pid "+str(work/"nginx.pid")+";\nerror_log "+str(work/"error.log")+";\n"
              "events {} http {\naccess_log off;\n"+
              "".join(key+" "+str(work/name)+";\n" for key,name in [
                  ("client_body_temp_path","client-temp"),("proxy_temp_path","proxy-temp"),
                  ("fastcgi_temp_path","fastcgi-temp"),("uwsgi_temp_path","uwsgi-temp"),
                  ("scgi_temp_path","scgi-temp")])+
              'map $request_uri $fixture_peer { default ""; ~[?&]client=(?<fixture_ip>[0-9.]+) $fixture_ip; }\n'
              "geo $fixture_peer $allowed { default 0; 127.0.0.1/32 1; 192.168.10.0/24 1; }\n"
              'map "$uri:$allowed" $rejected { default 0; "/firmware.bin:0" 1; }\n'+
              trace.http_config(peer="$fixture_peer")+
              "server { listen unix:"+str(sock)+";\n"+trace.server_config(log)+
              'error_page 403 /site-error;\nif ($rejected) { return 403; }\n'
              'location = /site-error { return 403 "denied"; }\nlocation / { return 200 "fixture"; }\n}}\n')
            path=work/"nginx.conf";path.write_text(config)
            args=["docker","run","--rm","--name",name,"--network","none",
                  "--user",str(os.getuid())+":"+str(os.getgid()),"-v",tmp+":"+tmp,
                  "--entrypoint","nginx",IMAGE,"-p",tmp+"/","-c",str(path),"-g","daemon off;"]
            process=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            class UnixHTTP(http.client.HTTPConnection):
                def connect(self):
                    self.sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
                    self.sock.settimeout(3);self.sock.connect(str(sock))
            try:
                until=time.monotonic()+10
                while not sock.exists() and time.monotonic()<until:
                    if process.poll() is not None:raise RuntimeError(process.stderr.read().decode()[-1000:])
                    time.sleep(0.05)
                self.assertTrue(sock.exists())
                def local_head(plan, uri):
                    client=UnixHTTP("fixture",timeout=3)
                    client.request("HEAD",uri+"?client=127.0.0.1")
                    response=client.getresponse();response.read();client.close()
                    self.assertEqual(200,response.status)
                    return response.status
                with patch.object(trace.guard,"head_local",side_effect=local_head) as head_probe:
                    self.assertTrue(trace.capture_self_check({},log,timeout=3)["actual_loopback_log_verified"])
                self_head_count=head_probe.call_count
                cases=[("GET","/version.json?client=192.168.23.27&token=NEVER_LOG",200,"ESP32HTTPClient"),
                       ("GET","/firmware.bin?client=192.168.23.27",403,"ESP32-http-Update"),
                       ("GET","/firmware.bin?client=203.0.113.9",403,'ESP32-http-Update"escaped'),
                       ("HEAD","/version.json?client=192.168.10.2",200,"fixture"),
                       ("HEAD","/firmware.bin?client=192.168.10.2",200,"fixture"),
                       ("HEAD","/firmware.bin?client=192.168.23.27",403,"ESP32-http-Update"),
                       ("GET","/firmware/target.sgkenc?token=NEVER_LOG",200,"ESP32HTTPClient"),
                       ("GET","/mobile/app.apk?token=NEVER_LOG",200,"fixture")]
                for method,uri,expected,agent in cases:
                    client=UnixHTTP("fixture",timeout=3)
                    client.request(method,uri,headers={"User-Agent":agent,"X-Forwarded-For":"192.168.23.27"})
                    response=client.getresponse();response.read();client.close()
                    self.assertEqual(expected,response.status)
                until=time.monotonic()+3
                while (not log.exists() or len(log.read_text().splitlines())<self_head_count+6) and time.monotonic()<until:
                    time.sleep(0.05)
                rows=trace.read_records(log)
                self.assertEqual(self_head_count+6,len(rows))
                self.assertEqual(self_head_count+3,sum(row["method"]=="HEAD" for row in rows))
                self.assertNotIn("NEVER_LOG",log.read_text())
                self.assertEqual("192.168.23.0/24",trace.repair_candidate(
                    rows,["192.168.10.0/24","192.168.23.0/24"],["192.168.10.0/24"]))
                self.assertTrue(all(row["uri"] in ("/version.json","/firmware.bin") for row in rows))
                self.assertTrue(any(row["peer"]=="203.0.113.9" for row in rows))
            finally:
                subprocess.run(["docker","rm","-f",name],capture_output=True,timeout=30)
                if process.poll() is None:process.terminate()
                process.communicate(timeout=15)

    def test_trace_loaders_are_harmless_after_volatile_files_disappear(self):
        with tempfile.TemporaryDirectory(prefix="smartbox-trace-reboot-") as tmp:
            path=Path(tmp)/"nginx.conf"
            path.write_text("events {} http { access_log off; include "+tmp+
                "/missing-runtime/http.*.conf; server { listen 127.0.0.1:8080; include "+
                tmp+"/missing-runtime/server.*.conf; return 204; } }")
            result=subprocess.run(["docker","run","--rm","--network","none",
                "-v",tmp+":"+tmp,"--entrypoint","nginx",IMAGE,"-t","-c",str(path)],
                capture_output=True,text=True,timeout=30)
            self.assertEqual(0,result.returncode,result.stderr)

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
