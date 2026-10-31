import importlib.util
import pathlib
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("power_monitor",ROOT/"ops/power_monitor.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def sample(**changes):
    data={"schema":1,"at":1000,"valid":1,"state":0,"interval_s":30,"voltage":12.6,"sample_age_ms":200}
    data.update(changes);return data

class PowerMonitorTests(unittest.TestCase):
    def test_valid_normal(self):self.assertEqual("NORMAL",m.evaluate(sample(),1020)["status"])
    def test_source_failure_is_not_battery_or_device_failure(self):
        self.assertEqual("SOURCE_UNAVAILABLE",m.evaluate(None,1020,source_ok=False)["status"])
    def test_stale_expected_low_power_interval(self):
        self.assertEqual("RECOVERY_WAIT",m.evaluate(sample(state=4,interval_s=300),1500)["status"])
        self.assertEqual("OFFLINE",m.evaluate(sample(state=4,interval_s=300),1631)["status"])
    def test_invalid_measurement_ignores_old_nominal_voltage(self):
        x=m.evaluate(sample(valid=0,voltage=12.2),1020)
        self.assertEqual("POWER_SENSOR_FAULT",x["status"]);self.assertIsNone(x["voltage_v"])
    def test_nonfinite_and_oversized_voltage(self):
        for value in [float('nan'),float('inf'),-1,18,27]:
            self.assertEqual("POWER_SENSOR_FAULT",m.evaluate(sample(voltage=value),1020)["status"])
    def test_future_receipt_is_invalid(self):self.assertEqual("SOURCE_INVALID",m.evaluate(sample(at=1100),1020)["status"])
    def test_legacy_firmware_is_distinct(self):self.assertEqual("LEGACY_FIRMWARE",m.evaluate(sample(schema=0),1020)["status"])
    def test_bad_state_or_interval_is_invalid(self):
        for row in [sample(state=99),sample(interval_s=1),sample(sample_age_ms=3000)]:
            self.assertIn(m.evaluate(row,1020)["status"],["SOURCE_INVALID","POWER_SENSOR_FAULT"])
    def test_warning_dwell_and_dedup(self):
        warning=m.evaluate(sample(state=1),1020)
        state,event=m.advance({},warning,1020);self.assertIsNone(event)
        state,event=m.advance(state,warning,1081);self.assertEqual("LOW_BATTERY",event["status"])
        state,event=m.advance(state,warning,1200);self.assertIsNone(event)
    def test_protection_immediate_and_recovery_once(self):
        state,event=m.advance({},m.evaluate(sample(state=3),1020),1020)
        self.assertEqual("BATTERY_PROTECTED",event["status"])
        state,event=m.advance(state,m.evaluate(sample(),1020),1021)
        self.assertEqual("RECOVERED",event["status"])
        state,event=m.advance(state,m.evaluate(sample(),1020),1022);self.assertIsNone(event)
    def test_first_normal_is_quiet(self):
        state,event=m.advance({},m.evaluate(sample(),1020),1020);self.assertIsNone(event)
    def test_flux_mixed_frame_is_rejected(self):
        csv='\n,result,table,_time,_field,_value\n,,0,2026-10-31T00:00:00Z,power_schema,1\n,,1,2026-10-30T00:00:00Z,power_state_code,0\n'

        with self.assertRaises(m.SourceFormatError):m.parse_flux(csv)
    def test_rollback_battery_frame_does_not_reuse_new_schema(self):
        raw=',result,table,_time,_field,_value\n,,0,2026-10-31T00:01:00Z,battery_v,12.5\n,,1,2026-10-31T00:00:00Z,power_schema,1\n'
        sample=m.parse_flux(raw)
        self.assertIsNotNone(sample);self.assertEqual(0,sample['schema'])
    def test_flux_coherent_nanoseconds_frame(self):
        fields={'power_schema':1,'power_sensor_valid':1,'power_state_code':4,'power_report_interval_s':300,'power_sample_age_ms':20,'battery_v':12.6}
        raw=',result,table,_time,_field,_value\n'+''.join(',,0,2026-10-31T00:00:00.123456789Z,%s,%s\n'%(k,v) for k,v in fields.items())
        sample=m.parse_flux(raw);self.assertEqual(4,sample['state']);self.assertEqual(12.6,sample['voltage'])
    def test_query_error_http200_is_source_failure(self):
        from unittest.mock import patch
        import io
        class Response(io.BytesIO):
            def __enter__(self):return self
            def __exit__(self,*a):self.close()
        class Opener:
            def open(self,*a,**k):return Response(b',error,reference\n,query failed,123\n')
        with patch.object(m.urllib.request,'build_opener',return_value=Opener()):
            with self.assertRaises(m.SourceResponseError):m.fetch_sample({'org':'o','bucket':'b','token':'fixture'})
            import tempfile,pathlib
            with tempfile.TemporaryDirectory() as tmp:
                state=m.cycle({'org':'o','bucket':'b','token':'fixture','recipient':'fixture'},pathlib.Path(tmp),now=1020,notifier=lambda *a:True)
                self.assertEqual('SOURCE_UNAVAILABLE',state['current']['status'])
    def test_bad_sample_ages_do_not_become_normal(self):
        for age in [float('nan'),-1,float('inf')]:
            self.assertNotEqual('NORMAL',m.evaluate(sample(sample_age_ms=age),1020)['status'])
    def test_legacy_stale_frame_is_offline_not_permanently_fresh_legacy(self):
        current=m.evaluate(sample(schema=0),1091)
        self.assertEqual('OFFLINE',current['status']);self.assertIsNone(current['voltage_v'])
        self.assertEqual('LEGACY_FIRMWARE',m.evaluate(sample(schema=0),1090)['status'])
    def test_legacy_to_offline_alert_is_once_without_claiming_measurement(self):
        state,event=m.advance({},m.evaluate(sample(schema=0),1020),1020)
        state,event=m.advance(state,m.evaluate(sample(schema=0),1091),1091)
        self.assertEqual('OFFLINE',event['status']);self.assertIsNone(event['voltage_v'])
        state,event=m.advance(state,m.evaluate(sample(schema=0),1100),1100);self.assertIsNone(event)
if __name__=='__main__':unittest.main()
