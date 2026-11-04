"""Battery alerts use MCU-qualified continuity, never repeated NAS receipt age."""
import importlib.util,pathlib,tempfile,json,unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('warning_monitor',ROOT/'ops/power_monitor.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def sample(**changes):
 d=dict(schema=1,at=1000,valid=1,state=0,interval_s=300,voltage=12.6,sample_age_ms=20,
        warning_schema=1,warning_active=0,warning_elapsed_s=0,warning_voltage_v=11.0,warning_required_s=300)
 d.update(changes);return d
class WarningPolicyTests(unittest.TestCase):
 def test_power_modes_never_raise_battery_alert_before_qualification(self):
  for code in [1,2,3,4]:
   state={}
   for now in [1020,1081,1300,1500]:
    current=m.evaluate(sample(state=code,voltage=11.5),now)
    state,event=m.advance(state,current,now)
    self.assertIsNone(event,(code,now,event))
 def test_qualified_11v_inclusive_alerts_once_without_extra_nas_delay(self):
  current=m.evaluate(sample(state=3,voltage=11,warning_active=1,warning_elapsed_s=300),1020)
  state,event=m.advance({},current,1020)
  self.assertEqual('LOW_BATTERY',event['status'])
  state,event=m.advance(state,current,1080);self.assertIsNone(event)
 def test_299_seconds_cannot_warn_even_if_nas_reuses_frame_for_minutes(self):
  current=m.evaluate(sample(state=3,voltage=11,warning_elapsed_s=299),1020)
  state={}
  for now in [1020,1080,1200,1500]:
   state,event=m.advance(state,current,now);self.assertIsNone(event)
 def test_voltage_recovery_clears_warning_without_claiming_protection_release(self):
  old=m.evaluate(sample(state=3,voltage=10.9,warning_active=1,warning_elapsed_s=300),1020)
  state,event=m.advance({},old,1020);self.assertEqual('LOW_BATTERY',event['status'])
  cur=m.evaluate(sample(state=3,voltage=11.1),1021)
  state,event=m.advance(state,cur,1021);self.assertEqual('RECOVERED',event['status'])
  self.assertEqual('BATTERY_PROTECTED',state['current']['status'])
 def test_missing_warning_contract_is_explicit_and_never_low_battery(self):
  x=sample()
  for k in list(x):
   if k.startswith('warning_'):del x[k]
  current=m.evaluate(x,1020)
  self.assertEqual('WARNING_POLICY_UNAVAILABLE',current['status'])
 def test_inconsistent_warning_proof_is_source_invalid(self):
  for changes in [dict(voltage=11.1,warning_active=1,warning_elapsed_s=300),
                  dict(warning_active=1,warning_elapsed_s=299,voltage=11),
                  dict(warning_voltage_v=11.3),dict(warning_required_s=60),
                  dict(warning_elapsed_s=float('nan')),dict(warning_elapsed_s=300.5)]:
   self.assertEqual('SOURCE_INVALID',m.evaluate(sample(**changes),1020)['status'],changes)
 def test_sensor_and_transport_faults_still_alert_immediately(self):
  for cur in [m.evaluate(sample(valid=0),1020),m.evaluate(sample(state=5),1020),m.evaluate(None,1020,source_ok=False),m.evaluate(None,1020)]:
   _,event=m.advance({},cur,1020);self.assertIsNotNone(event)
   self.assertNotEqual('LOW_BATTERY',event['status']);self.assertEqual('warning',event['severity'])
 def test_queued_obsolete_battery_alerts_are_not_delivered(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=pathlib.Path(tmp);(d/'state.json').write_text(json.dumps({'active':'NORMAL','pending':[{'id':'a'*16,'status':'LOW_BATTERY','severity':'warning','at':900,'voltage_v':11.5}]}))
   sent=[]
   with patch.object(m,'regular',lambda p:p.read_text()):
    state=m.cycle({'recipient':'fixture'},d,now=1020,fetch=lambda c:sample(),notifier=lambda e,r:sent.append(e) or True)
   self.assertEqual([],sent);self.assertEqual([],state['pending']);self.assertEqual(1,state['warning_policy_schema'])
 def test_warning_fields_cannot_mix_telemetry_cohorts(self):
  fields={'power_schema':1,'power_sensor_valid':1,'power_state_code':3,'power_report_interval_s':300,'power_sample_age_ms':20,'battery_v':11,
          'power_warning_schema':1,'power_low_warning_active':1,'power_low_warning_elapsed_s':300,'power_low_warning_voltage_v':11,'power_low_warning_required_s':300}
  raw=',result,table,_time,_field,_value\n'+''.join(',,0,2026-11-04T00:%s:00Z,%s,%s\n'%('01' if k=='power_low_warning_elapsed_s' else '00',k,v) for k,v in fields.items())
  with self.assertRaises(m.SourceFormatError):m.parse_flux(raw)
if __name__=='__main__':unittest.main()
