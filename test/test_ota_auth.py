import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import re
import tempfile
import unittest
import uuid
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
IMAGE="nginx@sha256:892bb2152464f0c5d8972593a5f5afaf8054552580b699d95f8ff4af08c2d33d"


def load():
    for name in ("install_ota_origin_guard","authenticated_ota_origin"):
        spec=importlib.util.spec_from_file_location(name,ROOT/"ops"/(name+".py"))
        module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module)
    return module


class OtaAuthPolicyTest(unittest.TestCase):
    def test_only_fixed_paths_original_peer_and_hashed_passwords(self):
        m=load();http,server=m.render({"allowed_cidrs":["127.0.0.1/32"]},
                                   " proxy_pass http://127.0.0.1:80;\n")
        self.assertIn("$realip_remote_addr",http)
        self.assertNotIn("$http_x_forwarded_for",http)
        self.assertEqual(2,server.count('proxy_set_header Authorization "";'))
        self.assertIn("location = /firmware.bin",server);self.assertIn("location = /version.json",server)
        with self.assertRaises(ValueError):m.render({"allowed_cidrs":[]},'',peer="$http_x_forwarded_for")
        with self.assertRaises(ValueError):m.render({"allowed_cidrs":[]},'',auth_file=Path("/bad;path"))
    def test_public_exception_is_one_ipv4_host_and_trace_bound(self):
        m=load()
        self.assertEqual("8.8.8.8/32 1;\n",m.bootstrap_entry("8.8.8.8"))
        for address in ("192.168.23.1","127.0.0.1","100.90.1.2","2001:4860:4860::8888","8.8.8.0/24"):
            with self.subTest(address=address),self.assertRaises(ValueError):m.bootstrap_entry(address)
        with tempfile.TemporaryDirectory() as tmp:
            work=Path(tmp)/"ota-trace-one";work.mkdir()
            plan={"report_key":"trace-example"}
            (work/"plan.json").write_text(json.dumps(plan))
            row={"at":"1.0","uri":"/firmware.bin","method":"GET","status":403,
                 "agent":"ESP32-http-Update","peer":"8.8.8.8"}
            (work/"requests.jsonl").write_text(json.dumps(row)+"\n")
            digest=m.guard.digest(json.dumps(plan,sort_keys=True).encode())
            with patch.object(m.guard,"regular",side_effect=lambda p:p):
                self.assertEqual("8.8.8.8",m.observed_public_peer(Path(tmp),"trace-example",digest,"1.0"))
                with self.assertRaises(ValueError):m.observed_public_peer(Path(tmp),"trace-example","0"*64,"1.0")
                with self.assertRaises(ValueError):m.observed_public_peer(Path(tmp),"trace-example",digest,"2.0")
                (work/"requests.jsonl").write_text(json.dumps(row)+"\n"+json.dumps(row)+"\n")
                with self.assertRaises(ValueError):m.observed_public_peer(Path(tmp),"trace-example",digest,"1.0")
    def test_public_clock_gate_expires_without_controller_cleanup(self):
        m=load();line=m.bootstrap_time_entry(1792120000)
        expression=line.split('"')[1][1:]
        for value in ("1792120000.000","1792120299.999"):
            self.assertRegex(value,expression)
        for value in ("1792119999.999","1792120300.000"):
            self.assertIsNone(re.fullmatch(expression,value))
        with self.assertRaises(ValueError):m.bootstrap_time_entry(1792120000,301)
    def test_credential_input_is_data_only_and_strictly_bounded(self):
        m=load();identity={"username":"ota-fixture","password":"a"*64}
        self.assertEqual(identity,m.credentials(json.dumps(identity)))
        for change in ({"password":""},{"password":"x"*64},{"username":"bad:newline"},{"extra":"field"}):
            with self.subTest(change=change),self.assertRaises(ValueError):m.credentials(json.dumps({**identity,**change}))


@unittest.skipUnless(os.getenv("RUN_NGINX_ORIGIN_INTEGRATION")=="1","isolated real nginx opt-in")
class OtaAuthNginxTest(unittest.TestCase):
    def run_fixture(self,bootstrap,expired=False):
        m=load()
        with tempfile.TemporaryDirectory(prefix="smartbox-ota-auth-") as tmp:
            work=Path(tmp);name="smartbox-auth-"+uuid.uuid4().hex[:12]
            subprocess.run(["docker","pull",IMAGE],capture_output=True,check=True,timeout=120)
            args=["docker","run","--rm","--name",name,"--network","none",
                  "--user",str(os.getuid())+":"+str(os.getgid()),"-v",tmp+":"+tmp,"--entrypoint","nginx",IMAGE]
            wrapper=work/"nginx-wrapper";wrapper.write_text("#!/bin/sh\nexec "+shlex.join(args)+' "$@"\n');wrapper.chmod(0o700)
            m.guard.NGINX=str(wrapper)
            identity={"username":"ota-fixture","password":"a"*64}
            auth_file=work/"htpasswd";auth_file.write_text(m.password_line(identity));auth_file.chmod(0o600)
            origin={"allowed_cidrs":["127.0.0.1/32","::1/128","192.168.10.0/24","100.64.0.0/10","192.168.23.0/24"]}
            try:
                count=m.fixture(origin,work,identity,auth_file,bootstrap_peer="8.8.8.8" if bootstrap else None,bootstrap_expired=expired)
                self.assertEqual(100 if bootstrap else 96,count)
            finally:subprocess.run(["docker","rm","-f",name],capture_output=True,timeout=30)
    def test_anonymous_wrong_valid_and_encoded_paths(self):self.run_fixture(False)
    def test_one_explicit_public_bootstrap_peer_preserves_other_denials(self):self.run_fixture(True)

    def test_expired_bootstrap_peer_is_denied_without_reload(self):self.run_fixture(True,True)
