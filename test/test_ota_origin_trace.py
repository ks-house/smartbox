import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
def load():
 for name in ["install_ota_origin_guard","update_ota_origin_guard","observe_ota_origin"]:
  spec=importlib.util.spec_from_file_location(name,ROOT/"ops"/(name+".py"))
  module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module)
 return module
class OriginTraceTest(unittest.TestCase):
 def test_only_actual_403_esp32_peer_on_one_missing_private_nas_lan_is_eligible(self):
  m=load();networks=["192.168.10.0/24","192.168.23.0/24"];allowed=["192.168.10.0/24"]
  valid={"peer":"192.168.23.27","status":403,"uri":"/firmware.bin","agent":"ESP32-http-Update"}
  self.assertEqual("192.168.23.0/24",m.repair_candidate([valid],networks,allowed))
  for change in [{"peer":"203.0.113.1"},{"peer":"192.168.99.5"},{"peer":"192.168.10.5"},
                 {"peer":"100.90.1.1"},{"status":200},{"status":401},{"uri":"/version.json"},
                 {"agent":"Mozilla/5.0"},{"peer":"::1"}]:
   with self.subTest(change=change):self.assertIsNone(m.repair_candidate([{**valid,**change}],networks,allowed))
 def test_multiple_private_candidates_and_broad_networks_do_not_expand_access(self):
  m=load();rows=[{"peer":p,"status":403,"uri":"/firmware.bin","agent":"ESP32"}
                 for p in ["192.168.23.2","192.168.24.2"]]
  with self.assertRaises(ValueError):m.repair_candidate(rows,["192.168.23.0/24","192.168.24.0/24"],[])
  self.assertIsNone(m.repair_candidate(rows,["192.168.0.0/16"],[]))
 def test_trace_omits_query_tokens_and_untrusted_forwarded_headers(self):
  m=load();text=m.http_config()
  self.assertIn("$realip_remote_addr",text);self.assertIn('"uri":"$uri"',text)
  for excluded in ["$request_uri","$args","$http_authorization","$http_x_forwarded_for"]:
   self.assertNotIn(excluded,text)
  with self.assertRaises(ValueError):m.http_config("$http_x_forwarded_for")
  with self.assertRaises(ValueError):m.server_config(Path("/tmp/bad;return"))
 def test_bounded_parser_and_redacted_summary(self):
  m=load()
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/"log";p.write_text('not-json\n'+json.dumps({"peer":"203.0.113.1","uri":"/firmware.bin",
     "status":403,"agent":"ESP32 secret-extra","at":"1.0"})+'\n')
   rows=m.read_records(p);summary=m.summary(rows,["192.168.23.0/24"])
   self.assertEqual(1,len(rows));self.assertEqual("PUBLIC",summary[0]["peer_scope"])
   self.assertNotIn("203.0.113.1",json.dumps(summary));self.assertNotIn("secret-extra",json.dumps(summary))
 def test_cleanup_preserves_concurrent_changes_and_is_idempotent(self):
  m=load()
  class File:
   alive=True;body=b"pinned";removed=False
   def exists(self):return self.alive
   def is_symlink(self):return False
   def stat(self):return SimpleNamespace(st_uid=0)
   def read_bytes(self):return self.body
   def unlink(self):self.alive=False;self.removed=True
  file=File()
  with patch.object(m.guard,"run") as run:
   with self.assertRaises(RuntimeError):m.remove_exact([(file,"changed")])
   self.assertFalse(file.removed)
   m.remove_exact([(file,"pinned")]);self.assertTrue(file.removed)
   self.assertEqual(2,run.call_count)
   m.remove_exact([(file,"pinned")]);self.assertEqual(2,run.call_count)
if __name__=="__main__":unittest.main()
