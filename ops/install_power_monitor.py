"""Install only the owner-approved SmartBox read-only observer; preserve other services."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import stat
import subprocess
import sys
import time
import types

ROOT=Path('/usr/local/lib/smartbox-power-monitor')
STATE=Path('/volume1/smartbox-operations-private/power-monitor')
UNIT=Path('/etc/systemd/system/smartbox-power-monitor.service')
SERVICE='smartbox-power-monitor.service'

def parse_collector(text,env):
    sections=re.split(r'(?m)^\[\[outputs\.influxdb_v2\]\]\s*$',text)
    if len(sections)!=2:raise ValueError('one existing collector identity required')
    section=re.split(r'(?m)^\[\[',sections[1],maxsplit=1)[0]
    result={}
    for key,out in [('token','token'),('organization','org'),('bucket','bucket')]:
        values=re.findall(r'(?m)^\s*'+key+r'\s*=\s*"([^"\n]*)"',section)
        if len(values)!=1:raise ValueError('closed existing identity')
        value=re.sub(r'\$\{([A-Za-z0-9_]+)\}',lambda x:env.get(x.group(1),''),values[0])
        if not value or len(value)>2048 or '\n' in value or '\r' in value:raise ValueError('existing identity unavailable')
        result[out]=value
    return result

def service_unit():
    return '''[Unit]
Description=SmartBox battery and telemetry observer
After=pkg-ContainerManager-dockerd.service network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
UMask=0077
ExecStart=/usr/bin/python3 -I -B /usr/local/lib/smartbox-power-monitor/monitor.py
Restart=on-failure
RestartSec=15
TimeoutStopSec=10
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
'''

def validate_candidate(path,expected):
    if not re.fullmatch(r'[a-f0-9]{64}',expected):raise ValueError('exact reviewed source digest')
    info=path.lstat()
    if path.is_symlink() or not stat.S_ISREG(info.st_mode) or info.st_size>65536 or stat.S_IMODE(info.st_mode)&0o022:raise ValueError('bounded protected candidate')
    raw=path.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=expected:raise ValueError('source digest mismatch')
    compile(raw,str(path),'exec')
    return raw

def command(*args,check=True):
    p=subprocess.run(['/bin/systemctl',*args],capture_output=True,timeout=20)
    if check and p.returncode:raise RuntimeError('observer service operation failed')
    return p.returncode

def existing_config(docker,recipient):
    c=docker.inspect('telegraf')
    mounts=[x for x in c['Mounts'] if x['Destination']=='/etc/telegraf/telegraf.conf']
    if len(mounts)!=1:raise ValueError('one current collector configuration')
    source=Path(mounts[0]['Source']);info=source.lstat()
    if source.is_symlink() or not stat.S_ISREG(info.st_mode) or info.st_size>131072 or stat.S_IMODE(info.st_mode)&0o022:raise ValueError('protected collector source')
    raw=source.read_bytes();env=dict(x.split('=',1) for x in c['Config'].get('Env',[]) if '=' in x)
    result=parse_collector(raw.decode(),env)
    result.update(recipient=recipient,state_directory=str(STATE),interval_seconds=60)
    return result,source,hashlib.sha256(raw).hexdigest()

def install(source,expected,recipient,apply=False):
    if os.geteuid()!=0:raise ValueError('owner-authorized root operator required')
    raw=validate_candidate(source,expected)
    sys.dont_write_bytecode=True
    monitor=types.ModuleType('candidate_power_monitor')
    exec(compile(raw,'<reviewed-power-monitor>','exec'),monitor.__dict__)
    spec=importlib.util.spec_from_file_location('broker_ops','/usr/local/lib/smart-gatekeeper-broker-ops/lib/activate.py')
    docker=importlib.util.module_from_spec(spec);spec.loader.exec_module(docker)
    config,collector,collector_sha=existing_config(docker,recipient);monitor.validate_config(config)
    sample=monitor.fetch_sample(config);current=monitor.evaluate(sample,time.time())
    if not apply:return {'preflight':True,'source_sha256':expected,'current':current}
    loaded=subprocess.run(['/bin/systemctl','show',SERVICE,'-p','LoadState'],capture_output=True,timeout=10)
    if loaded.stdout.strip()!=b'LoadState=not-found':raise ValueError('existing service binding must not be overwritten')
    if sample is None or sample.get('schema')!=1:raise ValueError('new installed telemetry schema required')
    return apply_candidate(raw,config,monitor,collector,collector_sha,expected)

def apply_candidate(raw,config,monitor,collector,collector_sha,expected):
    if ROOT.exists() or STATE.exists() or UNIT.exists():raise ValueError('observer already exists; do not overwrite without an explicit upgrade plan')
    if not Path('/usr/syno/bin/synodsmnotify').is_file():raise ValueError('existing owner-local notifier unavailable')
    created=[]
    try:
        STATE.parent.mkdir(mode=0o700,exist_ok=True)
        parent=STATE.parent.lstat()
        if STATE.parent.is_symlink() or parent.st_uid!=os.geteuid() or stat.S_IMODE(parent.st_mode)!=0o700:raise ValueError('private parent required')
        STATE.mkdir(mode=0o700);ROOT.mkdir(mode=0o755)
        for path,data,mode in [(ROOT/'monitor.py',raw,0o644),(ROOT/'config.json',json.dumps(config).encode(),0o600),(UNIT,service_unit().encode(),0o644)]:
            fd=os.open(str(path),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,mode)
            os.fchmod(fd,mode)
            with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
            created.append(path)
        monitor.atomic(STATE/'installation.json',{'source_sha256':expected,'collector_source_sha256':collector_sha,'prior_service':'absent','at':int(time.time())})
        command('daemon-reload');command('enable','--now',SERVICE)
        until=time.monotonic()+20
        while not (STATE/'state.json').is_file() and time.monotonic()<until:time.sleep(1)
        if command('is-active',SERVICE,check=False)!=0 or not (STATE/'state.json').is_file():raise RuntimeError('observer did not become active')
        observed=json.loads(monitor.regular(STATE/'state.json'))
        if observed['current']['status'] in ('SOURCE_UNAVAILABLE','SOURCE_INVALID'):raise RuntimeError('installed source check failed')
        if hashlib.sha256(collector.read_bytes()).hexdigest()!=collector_sha:raise RuntimeError('collector source changed')
        return {'installed':True,'source_sha256':expected,'service_active':True,'status':observed['current']['status'],
                'pending_notifications':len(observed.get('pending',[])),'collector_unchanged':True,'existing_credentials_reused':True}
    except Exception:
        command('stop',SERVICE,check=False);command('disable',SERVICE,check=False)
        for path in reversed(created):
            if path in (ROOT/'monitor.py',ROOT/'config.json',UNIT) and path.is_file() and not path.is_symlink():path.unlink()
        command('daemon-reload',check=False)
        if STATE.is_dir():monitor.atomic(STATE/'failed-installation.json',{'rolled_back':True,'source_sha256':expected,'at':int(time.time())})
        raise

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--expected-sha256',required=True);parser.add_argument('--recipient',required=True);parser.add_argument('--apply',action='store_true');args=parser.parse_args()
    try:print(json.dumps(install(args.source,args.expected_sha256,args.recipient,args.apply)))
    except Exception as e:print(json.dumps({'installed':False,'error_type':type(e).__name__}));raise SystemExit(1)
if __name__=='__main__':main()
