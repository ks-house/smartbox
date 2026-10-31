import importlib.util,pathlib,tempfile,time,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('first_transition',ROOT/'ops/first_battery_transition.py')
try:m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
except FileNotFoundError:m=None
class FirstTransitionTests(unittest.TestCase):
 def rows(self):
  return [dict(at=1000+i*30,voltage=12.14+i*0.001,current_ma=0.4,state='BATTERY_LOW_SHUTDOWN',uptime_ms=123000+i*30000,ota_pending=False) for i in range(4)]
 def test_exact_legacy_isolated_stable_first_transition(self):
  self.assertTrue(m.admit(self.rows(),1091,'0.1.0-ge0323a0')['accepted'])
 def test_stale_low_unstable_motion_reboot_and_wrong_version_refused(self):
  for key,value in [('voltage',11.9),('voltage',float('nan')),('voltage',16.1),('current_ma',10),('state','IDLE'),('ota_pending',True),('uptime_ms',1),('at',100)]:
   rows=self.rows();rows[-1][key]=value
   with self.assertRaises(ValueError,msg=key):m.admit(rows,1091,'0.1.0-ge0323a0')
  for now,version in [(1200,'0.1.0-ge0323a0'),(1091,'0.2.0-g1234567')]:
   with self.assertRaises(ValueError):m.admit(self.rows(),now,version)
 def test_legacy_synthetic_sensor_fallback_is_refused(self):
  rows=self.rows()
  for r in rows:r.update(voltage=12.2,current_ma=0.0)
  with self.assertRaises(ValueError):m.admit(rows,1091,'0.1.0-ge0323a0')
 def test_intent_prevents_any_repeat_and_only_ota_is_publishable(self):
  from unittest.mock import Mock
  with tempfile.TemporaryDirectory() as tmp:
   path=pathlib.Path(tmp)/'intent.json';client=Mock();client.publish.return_value.rc=0
   m.trigger_once(client,path,self.rows(),1091,'0.1.0-ge0323a0',{'version':'0.2.0-g1234567','commit':'1234567'+'a'*33,'sha256':'b'*64,'size':1400000})
   client.publish.assert_called_once_with('smartbox/command','{"command":"trigger_ota"}',qos=0,retain=False)
   with self.assertRaises(FileExistsError):m.trigger_once(client,path,self.rows(),1091,'0.1.0-ge0323a0',{'version':'0.2.0-g1234567','commit':'1234567'+'a'*33,'sha256':'b'*64,'size':1400000})
   self.assertEqual(client.publish.call_count,1)
 def test_malformed_candidate_cannot_reserve_or_publish(self):
  from unittest.mock import Mock
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp)/'intent.json';c=Mock()
   with self.assertRaises(ValueError):m.trigger_once(c,p,self.rows(),1091,'0.1.0-ge0323a0',{'version':'wrong'})
   c.publish.assert_not_called();self.assertFalse(p.exists())
 def test_any_fabricated_sensor_frame_is_refused(self):
  rows=self.rows()
  for r in rows:r['voltage']=12.18
  rows[1].update(voltage=12.2,current_ma=0.0)
  with self.assertRaises(ValueError):m.admit(rows,1091,'0.1.0-ge0323a0')
 def test_new_schema_and_not_immediate_frame_are_refused(self):
  rows=self.rows();rows[-1]['power_schema']=1
  with self.assertRaises(ValueError):m.admit(rows,1091,'0.1.0-ge0323a0')
  with self.assertRaises(ValueError):m.admit(self.rows(),1094,'0.1.0-ge0323a0')
 def test_parent_directory_fsync_failure_never_publishes(self):
  from unittest.mock import Mock,patch
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp)/'intent.json';c=Mock()
   with patch.object(m.os,'fsync',side_effect=[None,OSError('directory sync failed')]):
    with self.assertRaises(OSError):m.trigger_once(c,p,self.rows(),1091,'0.1.0-ge0323a0',{'version':'0.2.0-g1234567','commit':'1234567'+'a'*33,'sha256':'b'*64,'size':1400000})
   c.publish.assert_not_called();self.assertTrue(p.exists())
 def test_uncertain_publish_keeps_reservation(self):
  from unittest.mock import Mock
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp)/'intent.json';c=Mock();c.publish.return_value.rc=4
   with self.assertRaises(RuntimeError):m.trigger_once(c,p,self.rows(),1091,'0.1.0-ge0323a0',{'version':'0.2.0-g1234567','commit':'1234567'+'a'*33,'sha256':'b'*64,'size':1400000})
   self.assertTrue(p.exists())
   with self.assertRaises(FileExistsError):m.trigger_once(c,p,self.rows(),1091,'0.1.0-ge0323a0',{'version':'0.2.0-g1234567','commit':'1234567'+'a'*33,'sha256':'b'*64,'size':1400000})
   self.assertEqual(c.publish.call_count,1)
 def boot_rows(self):
  return [dict(at=1100,power_schema=1,power_sensor_valid=1,firmware='0.2.0-g1234567',uptime_ms=1000),dict(at=1400,power_schema=1,power_sensor_valid=1,firmware='0.2.0-g1234567',uptime_ms=301000)]
 def test_installed_acceptance_needs_continuous_new_online_boot(self):
  status={'at':1099,'status':'online','version':'0.2.0-g1234567','retained':False}
  self.assertTrue(m.accept_boot(self.boot_rows(),status,1092,123000000,'0.2.0-g1234567',1401))
  for change in [dict(uptime_ms=500),dict(uptime_ms=float('nan')),dict(power_sensor_valid=0),dict(at=1407),dict(firmware='wrong')]:
   rows=self.boot_rows();rows[-1].update(change)
   self.assertFalse(m.accept_boot(rows,status,1092,123000000,'0.2.0-g1234567',1401))
 def test_offline_retained_and_post_restart_rows_cannot_be_accepted(self):
  base={'at':1099,'status':'online','version':'0.2.0-g1234567','retained':False}
  for change in [dict(status='offline'),dict(retained=True),dict(at=1200),dict(at=1080)]:
   status={**base,**change};self.assertFalse(m.accept_boot(self.boot_rows(),status,1092,123000000,'0.2.0-g1234567',1401))
  rows=self.boot_rows();rows[-1]['at']=1101;rows[-1]['uptime_ms']=2000
  self.assertFalse(m.accept_boot(rows,base,1092,123000000,'0.2.0-g1234567',1102))
if __name__=='__main__':unittest.main()
