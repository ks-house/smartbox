import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class ActualOtaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.binary=pathlib.Path(cls.tmp.name)/'ota'
        args=['g++','-std=c++17','-pthread','-DNATIVE_BUILD','-I'+str(ROOT/'test/ota_stubs'),'-I'+str(ROOT/'src'),'-include',str(ROOT/'test/ota_stubs/Arduino.h'),str(ROOT/'src/AutoOtaManager.cpp'),str(ROOT/'src/SmartBoxController.cpp'),str(ROOT/'src/BatteryPolicy.cpp'),str(ROOT/'src/PowerManager.cpp'),str(ROOT/'test/ota_stubs/fixture.cpp'),'-o',str(cls.binary)]
        result=subprocess.run(args,capture_output=True,text=True,timeout=30)
        if result.returncode:raise AssertionError(result.stderr)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def run_case(self,name):
        result=subprocess.run([str(self.binary),name],capture_output=True,text=True,timeout=10)
        self.assertEqual(0,result.returncode,result.stdout[-1200:]+result.stderr)
    def test_actual_no_update_preserves_protection(self):self.run_case('no_update_fault')
    def test_preflash_contact_delay_power_drop_never_flashes(self):self.run_case('contact_fault')
    def test_immediate_preflash_admission_is_current(self):self.run_case('last_admission_fault')
    def test_daily_reboot_cannot_interrupt_manifest(self):self.run_case('reboot_guard')
    def test_close_during_actual_updater_is_denied(self):self.run_case('close_flash')
    def test_busy_during_manifest_does_not_suspend_motion_timeout(self):self.run_case('busy_manifest')
    def test_missed_night_check_catches_up_once_when_charged(self):self.run_case('catchup')
    def test_explicit_recovery_without_sensor_keeps_protection_on_no_update(self):self.run_case('recovery_no_update')
    def test_recovery_failed_flash_keeps_protection_and_consumes_once(self):self.run_case('recovery_fail')
    def test_recovery_manifest_failure_or_same_version_releases_reservation(self):
        self.run_case('recovery_manifest_fail');self.run_case('recovery_current')
    def test_recovery_preflash_known_low_voltage_never_flashes(self):self.run_case('recovery_known_low')
    def test_normal_manual_update_still_rejects_sensor_fault(self):self.run_case('normal_sensor_fault')
    def test_expired_recovery_never_contacts_or_flashes(self):
        self.run_case('recovery_expiry_queued');self.run_case('recovery_expiry_after_manifest')
    def test_recovery_expiry_at_final_preflash_gate_never_flashes(self):self.run_case('recovery_expiry_final')
    def test_recovery_can_replace_unreserved_normal_or_expired_queue(self):
        self.run_case('recovery_preempt_normal');self.run_case('recovery_replace_expired')
    def test_concurrent_owner_recovery_requests_reserve_once(self):self.run_case('recovery_concurrent')
if __name__=='__main__':unittest.main()
