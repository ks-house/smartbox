import importlib.util,pathlib,tempfile,hashlib,json,os,unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('upgrade_monitor',ROOT/'ops/update_power_monitor.py')
try:m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
except FileNotFoundError:m=None
def sha(x):return hashlib.sha256(x).hexdigest()
class MonitorUpgradeTests(unittest.TestCase):
 def setup_files(self,base):
  root=base/'program';root.mkdir();config=root/'config.json';config.write_text('{"fixture":"preserved"}');config.chmod(0o600)
  old=b'# known old program\n';new=b'# reviewed new program\n'
  program=root/'monitor.py';program.write_bytes(old);program.chmod(0o644)
  unit=base/'observer.service';unit.write_text('fixture-unit');unit.chmod(0o644)
  private=base/'private';private.mkdir(mode=0o700);state=private/'power-monitor';state.mkdir(mode=0o700)
  (state/'state.json').write_text(json.dumps({'fixture':'original-state'}));(state/'state.json').chmod(0o600)
  return dict(PROGRAM=program,CONFIG=config,UNIT=unit,STATE=state,BACKUP=private/'backup',OWNER_UID=os.getuid()),old,new
 def run_case(self,mode):
  self.assertIsNotNone(m,'upgrade operator not implemented')
  with tempfile.TemporaryDirectory() as tmp:
   base=pathlib.Path(tmp);paths,old,new=self.setup_files(base);calls=[];stage={'new':False}
   def command(action,check=True):
    calls.append(action)
    if mode=='stop-failure' and action=='stop' and not stage['new']:
     stage['new']=True;raise RuntimeError('injected stop failure')
    if mode=='start-failure' and action=='start' and not stage['new']:
     stage['new']=True;raise RuntimeError('injected start failure')
    return 0
   def observe():
    if mode=='post-failure' and (paths['PROGRAM'].read_bytes()==new):raise RuntimeError('injected post failure')
    return {'current':{'status':'NORMAL','battery_warning_active':False},'observed_at':99999999999,'pending':[],'warning_policy_schema':0 if mode=='stale-worker' else 1}
   with patch.multiple(m,**paths),patch.object(m,'command',command),patch.object(m,'probe',return_value={'status':'NORMAL','battery_warning_active':False}),patch.object(m,'observe',side_effect=observe),patch.object(m.time,'monotonic',side_effect=[0,0,30]):
    if mode in ['start-failure','post-failure','stop-failure','stale-worker']:
     with self.assertRaises(RuntimeError):m.upgrade(new,sha(old),sha(new),sha(b'fixture-unit'),apply=True)
     self.assertEqual(old,paths['PROGRAM'].read_bytes())
     self.assertEqual({'fixture':'original-state'},json.loads((paths['STATE']/'state.json').read_text()))
     self.assertGreaterEqual(calls.count('start'),2 if mode=='start-failure' else 1)
    elif mode=='wrong-old':
     with self.assertRaises(ValueError):m.upgrade(new,'0'*64,sha(new),sha(b'fixture-unit'),apply=True)
     self.assertEqual([],calls);self.assertEqual(old,paths['PROGRAM'].read_bytes())
    else:
     result=m.upgrade(new,sha(old),sha(new),sha(b'fixture-unit'),apply=True)
     self.assertTrue(result['updated']);self.assertEqual(new,paths['PROGRAM'].read_bytes())
     self.assertEqual(['stop','start'],[a for a in calls if a in ('stop','start')])
     self.assertEqual(old,(paths['BACKUP']/'monitor.py').read_bytes())
     again=m.upgrade(new,sha(old),sha(new),sha(b'fixture-unit'),apply=True)
     self.assertTrue(again['already_current']);self.assertEqual(['stop','start'],[a for a in calls if a in ('stop','start')])
    self.assertEqual('{"fixture":"preserved"}',paths['CONFIG'].read_text())
 def test_exact_upgrade_and_idempotent_retry(self):self.run_case('success')
 def test_stop_failure_restores_old_service(self):self.run_case('stop-failure')
 def test_old_worker_state_cannot_accept_new_policy(self):self.run_case('stale-worker')
 def test_wrong_original_never_stops_service(self):self.run_case('wrong-old')
 def test_start_failure_restores_program_and_state(self):self.run_case('start-failure')
 def test_post_failure_restores_program_and_state(self):self.run_case('post-failure')
if __name__=='__main__':unittest.main()
