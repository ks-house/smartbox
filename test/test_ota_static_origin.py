import importlib.util,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('repair',ROOT/'ops/repair_ota_static_origin.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
GUARD='''location = /version.json {
 auth_basic "OTA";
 auth_basic_user_file /private/htpasswd;
 limit_except GET HEAD { deny all; }
 proxy_set_header Authorization "";
 proxy_pass http://172.17.0.2:80;
}
'''+'''location = /firmware.bin {
 auth_basic "OTA";
 auth_basic_user_file /private/htpasswd;
 limit_except GET HEAD { deny all; }
 proxy_set_header Authorization "";
 proxy_pass http://172.17.0.2:80;
}
'''
class StaticOriginTests(unittest.TestCase):
 def test_only_content_handler_changes(self):
  value=m.render(GUARD,pathlib.Path('/volume1/docker/fixture'))
  self.assertIn('root /volume1/docker/fixture;',value);self.assertIn('disable_symlinks on;',value)
  for line in GUARD.splitlines():
   if 'proxy_pass' not in line:self.assertIn(line,value)
  self.assertNotIn('proxy_pass',value)
 def test_ambiguous_unsecured_or_injected_source_is_refused(self):
  for text,path in [(GUARD+GUARD,'/volume1/docker/fixture'),(GUARD.replace('auth_basic_user_file','other'),' /volume1/x'),(GUARD,'/etc'),(GUARD,'/volume1/x; auth_basic off')]:
   with self.assertRaises(ValueError):m.render(text,pathlib.Path(path))
 def test_candidate_keeps_other_sites_and_relative_prefix(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=pathlib.Path(tmp);sites=base/'sites';sites.mkdir();portal=sites/'chosen';other=sites/'other';portal.write_text('chosen');other.write_text('other')
   source='events {}\nhttp { include sites-enabled/*; }'
   value=m.global_candidate(source,sites,portal,base/'portal.candidate')
   self.assertIn(str(other),value);self.assertIn(str(base/'portal.candidate'),value);self.assertNotIn('sites-enabled/*',value)
 def test_manifest_and_binary_are_both_bound(self):
  import hashlib,json
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);web=root/'web';old=root/'backup';web.mkdir();old.mkdir()
   raw=b'\xe9old';meta=b'{"version":"0.1.0-g1234567"}'
   for p in [web,old]:(p/'firmware.bin').write_bytes(raw);(p/'version.json').write_bytes(meta)
   baseline={'version':'0.1.0-g1234567','sha256':hashlib.sha256(raw).hexdigest(),'backup_directory':str(old)}
   self.assertEqual(m.bound_artifacts(web,baseline)['version.json'],meta)
   (web/'version.json').write_text('{"version":"0.2.0-g7654321"}')
   with self.assertRaises(ValueError):m.bound_artifacts(web,baseline)
 def test_failed_runtime_confirmation_restores_both_bindings(self):
  from unittest.mock import patch
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);guard=root/'guard';config=root/'config';guard.write_bytes(b'old');config.write_bytes(b'old-config')
   info=guard.stat();ci=config.stat()
   def failure():raise RuntimeError('live route remains old')
   with patch.object(m,'checked') as run:
    with self.assertRaises(RuntimeError):m.activate_guard('nginx',guard,b'old',b'new',info,config,b'old-config',b'new-config',ci,failure)
    self.assertGreaterEqual(run.call_count,4)
   self.assertEqual(guard.read_bytes(),b'old');self.assertEqual(config.read_bytes(),b'old-config')
 def test_success_requires_runtime_confirmation_before_binding(self):
  from unittest.mock import patch
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);guard=root/'guard';config=root/'config';guard.write_bytes(b'old');config.write_bytes(b'old-config');observed=[]
   def verify():observed.append(config.read_bytes())
   with patch.object(m,'checked'):m.activate_guard('nginx',guard,b'old',b'new',guard.stat(),config,b'old-config',b'new-config',config.stat(),verify)
   self.assertEqual(observed,[b'old-config']);self.assertEqual(config.read_bytes(),b'new-config')
if __name__=='__main__':unittest.main()
