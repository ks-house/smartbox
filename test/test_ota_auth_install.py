import importlib.util
import json
import os
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
import uuid
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]


def load():
    for name in ("install_ota_origin_guard","update_ota_origin_guard","observe_ota_origin",
                 "authenticated_ota_origin","install_authenticated_ota","bootstrap_authenticated_ota"):
        spec=importlib.util.spec_from_file_location(name,ROOT/"ops"/(name+".py"))
        module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module)
    return sys.modules["install_authenticated_ota"],module


class AuthActivationTest(unittest.TestCase):
    def run_activation(self,fail):
        install,_=load()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);http=root/"http";server=root/"server";config=root/"config"
            http.write_bytes(b"legacy-http");server.write_bytes(b"legacy-server")
            config_bytes=json.dumps({"owner":"operator","origin_guard_hashes":{
                str(http):"old-http",str(server):"old-server"}}).encode()
            config.write_bytes(config_bytes)
            originals={p:p.read_bytes() for p in (http,server)}
            infos={p:p.stat() for p in originals};new={http:b"auth-http",server:b"auth-server"}
            def post(*_):
                if fail:raise RuntimeError("fixture postcheck failure")
                return 200
            with patch.object(install.updater,"OPS_CONFIG",config),patch.object(install.guard,"run"),patch.object(install.guard,"head_local",side_effect=post),patch.object(install.time,"sleep"):
                if fail:
                    with self.assertRaises(RuntimeError):install.activate({},originals,infos,config_bytes,config.stat(),new)
                    self.assertEqual(config_bytes,config.read_bytes())
                    self.assertTrue(all(p.read_bytes()==data for p,data in originals.items()))
                else:
                    returned=install.activate({},originals,infos,config_bytes,config.stat(),new)
                    self.assertEqual(returned,config.read_bytes())
                    expected={str(p):install.guard.digest(data) for p,data in new.items()}
                    self.assertEqual(expected,json.loads(returned)["origin_guard_hashes"])
                    self.assertTrue(all(p.read_bytes()==data for p,data in new.items()))
    def test_failed_postcheck_restores_exact_prior_profile(self):self.run_activation(True)
    def test_success_rebinds_only_after_live_checks(self):self.run_activation(False)
    def test_private_pinned_credential_input_rejects_readable_or_symlink_data(self):
        install,_=load()
        base=Path("/tmp")/("sgk-ota-auth-stage-unit-"+uuid.uuid4().hex)
        base.mkdir(mode=0o700)
        path=base/"identity.json";raw=json.dumps({"username":"ota-fixture","password":"a"*64}).encode()
        try:
            path.write_bytes(raw);path.chmod(0o600)
            digest=install.guard.digest(raw);owner=SimpleNamespace(pw_uid=os.getuid())
            self.assertEqual(raw,install.read_identity(path,digest,owner)[1])
            path.chmod(0o644)
            with self.assertRaises(ValueError):install.read_identity(path,digest,owner)
            path.chmod(0o600)
            with self.assertRaises(ValueError):install.read_identity(path,"0"*64,owner)
            path.unlink();target=base/"target";target.write_bytes(raw);target.chmod(0o600);path.symlink_to(target)
            with self.assertRaises(OSError):install.read_identity(path,digest,owner)
        finally:
            for item in base.iterdir():item.unlink()
            base.rmdir()


class BootstrapBindingTest(unittest.TestCase):
    def test_artifact_must_match_version_digest_identity_and_client_schema(self):
        _,bootstrap=load();identity={"username":"ota-fixture","password":"a"*64}
        image=b"0.1.0-gfixture ota-fixture "+b"a"*64+b" HTTPS authentication client schema=1"
        command={"expected_version":"0.1.0-gfixture","expected_sha256":bootstrap.hashlib.sha256(image).hexdigest()}
        def read(origin,uri,limit):
            return json.dumps({"version":"0.1.0-gfixture"}).encode() if uri=="/version.json" else image
        with patch.object(bootstrap,"local_read",side_effect=read):
            self.assertEqual(len(image),bootstrap.verify_artifact({},identity,command)[2])
            with self.assertRaises(ValueError):bootstrap.verify_artifact({},identity,{**command,"expected_sha256":"0"*64})
            with self.assertRaises(ValueError):bootstrap.verify_artifact({},{"username":"other","password":"b"*64},command)
    def test_download_is_not_authenticated_acceptance_or_installation(self):
        _,m=load();identity={"username":"ota-fixture","password":"a"*64}
        row={"uri":"/version.json","method":"GET","status":200,"agent":"ESP32HTTPClient",
             "peer":"8.8.8.8","user":"ota-fixture","realm":"SmartBox OTA"}
        self.assertTrue(m.authenticated_check(row,identity,"8.8.8.8"))
        self.assertFalse(m.authenticated_check({**row,"realm":"off"},identity,"8.8.8.8"))
        self.assertFalse(m.authenticated_check({**row,"user":"other"},identity,"8.8.8.8"))
        self.assertFalse(m.authenticated_check(row,identity,"1.1.1.1"))
        transfer={"uri":"/firmware.bin","method":"GET","status":200,"agent":"ESP32-http-Update","bytes":1024}
        self.assertTrue(m.matching_transfer(transfer,1024))
        self.assertFalse(m.matching_transfer({**transfer,"bytes":512},1024))
        self.assertFalse(m.matching_transfer({**transfer,"status":403},1024))
        self.assertFalse(m.authenticated_check(transfer,identity,"8.8.8.8"))
    def test_log_excludes_raw_queries_and_authorization(self):
        _,m=load();http,location=m.observation_config(Path("/tmp/fixture-log"))
        log_format=http.split("log_format",1)[1]
        for forbidden in ("$http_authorization","$request_uri","$args"):
            self.assertNotIn(forbidden,log_format)
        self.assertIn("$remote_user",log_format);self.assertIn("$body_bytes_sent",log_format)
