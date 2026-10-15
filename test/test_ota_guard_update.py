import importlib.util
from pathlib import Path
import unittest
import tempfile
import json
import os
import pwd
from types import SimpleNamespace
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
def load():
    import sys
    spec=importlib.util.spec_from_file_location("install_ota_origin_guard",ROOT/"ops/install_ota_origin_guard.py")
    guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
    sys.modules["install_ota_origin_guard"]=guard
    spec=importlib.util.spec_from_file_location("origin_update",ROOT/"ops/update_ota_origin_guard.py")
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module
class OriginUpdateTest(unittest.TestCase):
 def test_private_addition_keeps_original_and_rejects_broad_or_public_ranges(self):
    m=load();plan={"allowed_cidrs":["127.0.0.1/32","192.168.10.0/24"]}
    added=m.add_lan(plan,"192.168.23.0/24")
    self.assertEqual(["127.0.0.1/32","192.168.10.0/24","192.168.23.0/24"],added["allowed_cidrs"])
    self.assertEqual(2,len(plan["allowed_cidrs"]))
    for cidr in ["0.0.0.0/0","203.0.113.1/32","192.168.0.0/16",
                 "192.168.23.1/24","127.0.0.1/32","::1/128","192.168.10.0/24"]:
     with self.subTest(cidr=cidr),self.assertRaises(ValueError):m.add_lan(plan,cidr)
 def test_candidate_replaces_only_pinned_geo_and_preserves_relative_context(self):
    m=load();geo=Path("/etc/nginx/conf.d/http.guard.conf")
    other=Path("/etc/nginx/conf.d/http.other.conf")
    text="http { include conf.d/http.*.conf; include sites-enabled/*; }"
    result=m.expand_http_includes(text,[geo,other],geo,Path("/private/candidate.conf"))
    self.assertIn("include /private/candidate.conf;",result)
    self.assertIn("include /etc/nginx/conf.d/http.other.conf;",result)
    self.assertIn("include sites-enabled/*;",result)
    self.assertNotIn("include /etc/nginx/conf.d/http.guard.conf;",result)
    with self.assertRaises(ValueError):m.expand_http_includes(text,[other],geo,Path("/private/candidate.conf"))
    with self.assertRaises(ValueError):m.expand_http_includes(text+text,[geo],geo,Path("/private/candidate.conf"))
class OriginActivationTest(unittest.TestCase):
 def setup_fixture(self, root):
    m=load();guard=m.guard
    m.MAIN_CONFIG_DIRECTORY=root;m.REPORT_DIRECTORY=root/"reports";m.REPORT_DIRECTORY.mkdir()
    m.OPS_CONFIG=root/"gateway.json"
    class Private:
     def __str__(self):return str(root)
     def is_symlink(self):return False
     def stat(self):return SimpleNamespace(st_uid=0)
    m.PRIVATE=Private()
    guard.HTTP_INCLUDE=root/"http.guard.conf"
    directory=root/"service";directory.mkdir()
    plan={"allowed_cidrs":["127.0.0.1/32","192.168.10.0/24"],"guard_directory":str(directory),
          "report_owner":pwd.getpwuid(os.getuid()).pw_name,"report_key":"fixture-origin",
          "sources":{"global":{"path":str(root/"global.conf")}}}
    geo,rule=guard.render(plan);guard.HTTP_INCLUDE.write_text(geo)
    (directory/"user.conf.smartbox_ota_origin_guard").write_text(rule)
    (root/"global.conf").write_text("events {} http { include conf.d/http.*.conf; }")
    expected={str(p):guard.digest(p.read_bytes()) for p in
              [guard.HTTP_INCLUDE,directory/"user.conf.smartbox_ota_origin_guard"]}
    config={"owner":plan["report_owner"],"origin_guard_hashes":expected,"preserved":"fixture"}
    m.OPS_CONFIG.write_text(json.dumps(config))
    update={"origin_plan":plan,"expected_guard_hashes":expected,"additional_lan_cidr":"192.168.23.0/24"}
    return m,update,geo.encode(),m.OPS_CONFIG.read_bytes()
 def run_fixture(self, m, update, run, head):
    with patch.object(m.os,"geteuid",return_value=0),\
         patch.object(m,"protected_directory"),\
         patch.object(m.guard,"regular",side_effect=lambda p:p),\
         patch.object(m.guard,"read_plan",return_value=update["origin_plan"]),\
         patch.object(m.guard,"read_plan_from_sources"),\
         patch.object(m.guard,"fixture",return_value=30),\
         patch.object(m.guard,"run",side_effect=run),\
         patch.object(m.guard,"head_local",side_effect=head):
     m.execute(update,True)
 def test_failed_postcheck_restores_exact_geo_and_gateway_bytes(self):
    with tempfile.TemporaryDirectory() as tmp:
     m,u,geo,config=self.setup_fixture(Path(tmp));calls=[]
     def head(*args):
      calls.append(args)
      if len(calls)==3:raise RuntimeError("fixture postcheck failure")
     with self.assertRaises(RuntimeError):
      self.run_fixture(m,u,lambda *args:None,head)
     self.assertEqual(geo,m.guard.HTTP_INCLUDE.read_bytes())
     self.assertEqual(config,m.OPS_CONFIG.read_bytes())
 def test_success_rebinds_only_origin_hash_after_reload(self):
    with tempfile.TemporaryDirectory() as tmp:
     m,u,geo,config=self.setup_fixture(Path(tmp));commands=[]
     self.run_fixture(m,u,lambda args:commands.append(args),lambda *args:None)
     new=json.loads(m.OPS_CONFIG.read_bytes());old=json.loads(config)
     self.assertEqual("fixture",new["preserved"])
     self.assertEqual(set(old),set(new))
     self.assertEqual(m.guard.digest(m.guard.HTTP_INCLUDE.read_bytes()),
                      new["origin_guard_hashes"][str(m.guard.HTTP_INCLUDE)])
     self.assertNotEqual(geo,m.guard.HTTP_INCLUDE.read_bytes())
     self.assertTrue(any(args[-2:]==["-s","reload"] for args in commands))
 def test_stale_gateway_binding_refuses_before_guard_change(self):
    with tempfile.TemporaryDirectory() as tmp:
     m,u,geo,config=self.setup_fixture(Path(tmp))
     d=json.loads(config);d["origin_guard_hashes"]={};m.OPS_CONFIG.write_text(json.dumps(d))
     with self.assertRaises(ValueError):
      self.run_fixture(m,u,lambda *args:None,lambda *args:None)
     self.assertEqual(geo,m.guard.HTTP_INCLUDE.read_bytes())

if __name__=="__main__":unittest.main()
