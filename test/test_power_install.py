import importlib.util,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('install',ROOT/'ops/install_power_monitor.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class InstallTests(unittest.TestCase):
    def test_collector_toml_identity_is_parsed_without_output(self):
        config=m.parse_collector('[[outputs.influxdb_v2]]\n token="fake-token"\n organization="org"\n bucket="box"\n',{})
        self.assertEqual('box',config['bucket']);self.assertEqual('fake-token',config['token'])
    def test_missing_or_ambiguous_identity_refused(self):
        for text in ['', '[[outputs.influxdb_v2]]\n[[outputs.influxdb_v2]]\n']:
            with self.assertRaises(ValueError):m.parse_collector(text,{})
    def test_environment_expansion_requires_existing_identity(self):
        text='[[outputs.influxdb_v2]]\n token="${TOKEN}"\n organization="org"\n bucket="box"\n'
        with self.assertRaises(ValueError):m.parse_collector(text,{})
        self.assertEqual('existing',m.parse_collector(text,{'TOKEN':'existing'})['token'])
    def test_service_restarts_only_new_observer(self):
        unit=m.service_unit();self.assertIn('/usr/local/lib/smartbox-power-monitor/monitor.py',unit)
        self.assertNotIn('docker',unit.split('ExecStart=')[1].splitlines()[0]);self.assertIn('NoNewPrivileges=true',unit)
    def test_invalid_candidate_cannot_write_targets(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/'monitor.py';p.write_text('not python !!!')
            with self.assertRaises((ValueError,SyntaxError)):m.validate_candidate(p,'0'*64)
            self.assertEqual(['monitor.py'],[x.name for x in pathlib.Path(tmp).iterdir()])
    def test_service_start_failure_rolls_back_only_new_files(self):
        from unittest.mock import patch
        import types,hashlib,json
        with tempfile.TemporaryDirectory() as tmp:
            base=pathlib.Path(tmp);root=base/'program';state=base/'private'/'state';unit=base/'observer.service'
            collector=base/'collector';collector.write_text('preserved');sha=hashlib.sha256(collector.read_bytes()).hexdigest()
            calls=[]
            def command(*args,check=True):
                calls.append(args)
                if args[0]=='enable':raise RuntimeError('injected service failure')
                return 0
            def atomic(path,value):path.write_text(json.dumps(value))
            monitor=types.SimpleNamespace(atomic=atomic)
            with patch.multiple(m,ROOT=root,STATE=state,UNIT=unit),patch.object(m,'command',command),patch.object(m.Path,'is_file',autospec=True,side_effect=lambda p:True if str(p)=='/usr/syno/bin/synodsmnotify' else p.exists() and not p.is_dir()):
                with self.assertRaises(RuntimeError):m.apply_candidate(b'# harmless',{'token':'private'},monitor,collector,sha,'a'*64)
            self.assertFalse(unit.exists());self.assertFalse((root/'config.json').exists());self.assertFalse((root/'monitor.py').exists())
            self.assertEqual('preserved',collector.read_text())
            self.assertIn(('stop','smartbox-power-monitor.service'),calls)
            self.assertFalse(any('mosquitto' in str(call) or 'grafana' in str(call) for call in calls))
    def test_bootstrap_accepts_healthy_source_legacy_or_offline_only_explicitly(self):
        for sample,status in [(None,'OFFLINE'),({'schema':0},'LEGACY_FIRMWARE')]:
            with self.assertRaises(ValueError):m.validate_observer_admission(sample,{'status':status},False)
            m.validate_observer_admission(sample,{'status':status},True)
        for status in ['NORMAL','LOW_BATTERY','ENERGY_SAVING','BATTERY_PROTECTED','RECOVERY_WAIT','POWER_SENSOR_FAULT','INITIALIZING']:
            m.validate_observer_admission({'schema':1},{'status':status},False)
    def test_bootstrap_never_bypasses_invalid_or_failed_source(self):
        for status in ['SOURCE_INVALID','SOURCE_UNAVAILABLE','unrecognized']:
            with self.assertRaises(ValueError):m.validate_observer_admission(None,{'status':status},True)
        with self.assertRaises(ValueError):m.validate_observer_admission(None,{'status':'OFFLINE'},'yes')
    def test_enable_then_start_supports_systemd219_without_now_option(self):
        from unittest.mock import patch
        import types,hashlib,json
        with tempfile.TemporaryDirectory() as tmp:
            base=pathlib.Path(tmp);root=base/'program';state=base/'private'/'state';unit=base/'observer.service'
            collector=base/'collector';collector.write_text('preserved');sha=hashlib.sha256(collector.read_bytes()).hexdigest();calls=[]
            def command(*args,check=True):
                calls.append(args)
                if '--now' in args:raise RuntimeError('systemd219 unrecognized option --now')
                if args[0]=='start':(state/'state.json').write_text(json.dumps({'current':{'status':'OFFLINE'},'pending':[]}))
                return 0
            monitor=types.SimpleNamespace(atomic=lambda p,v:p.write_text(json.dumps(v)),regular=lambda p:p.read_text())
            with patch.multiple(m,ROOT=root,STATE=state,UNIT=unit),patch.object(m,'command',command),patch.object(m.Path,'is_file',autospec=True,side_effect=lambda p:True if str(p)=='/usr/syno/bin/synodsmnotify' else p.exists() and not p.is_dir()):
                result=m.apply_candidate(b'# harmless',{'token':'fake'},monitor,collector,sha,'a'*64)
            self.assertTrue(result['installed']);self.assertLess(calls.index(('enable',m.SERVICE)),calls.index(('start',m.SERVICE)))
            self.assertEqual('preserved',collector.read_text())
if __name__=='__main__':unittest.main()

