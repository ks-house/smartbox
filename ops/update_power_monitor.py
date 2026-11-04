"""Bounded, hash-pinned replacement of only the existing SmartBox observer."""
from pathlib import Path
import hashlib,json,os,re,stat,subprocess,tempfile,time,types

PROGRAM=Path('/usr/local/lib/smartbox-power-monitor/monitor.py')
CONFIG=PROGRAM.with_name('config.json')
STATE=Path('/volume1/smartbox-operations-private/power-monitor')
UNIT=Path('/etc/systemd/system/smartbox-power-monitor.service')
BACKUP=Path('/volume1/smartbox-operations-private/warning-upgrade-20261104')
SERVICE='smartbox-power-monitor.service'
OWNER_UID=0

def digest(raw):return hashlib.sha256(raw).hexdigest()

def protected_dir(path):
    info=path.lstat()
    if path.is_symlink() or not stat.S_ISDIR(info.st_mode) or info.st_uid!=OWNER_UID or stat.S_IMODE(info.st_mode)&0o022:
        raise ValueError('protected fixed directory required')

def read(path,private=False,limit=262144):
    fd=os.open(str(path),os.O_RDONLY|os.O_NOFOLLOW)
    try:
        info=os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid!=OWNER_UID or info.st_nlink!=1 or info.st_size>limit or stat.S_IMODE(info.st_mode)&(0o077 if private else 0o022):
            raise ValueError('protected bounded regular file required')
        with os.fdopen(fd,'rb',closefd=False) as stream:raw=stream.read(limit+1)
        if len(raw)>limit:raise ValueError('file bound')
        return raw
    finally:os.close(fd)

def atomic(path,raw,mode):
    fd,name=tempfile.mkstemp(prefix='.warning-',dir=str(path.parent))
    try:
        os.fchmod(fd,mode)
        with os.fdopen(fd,'wb') as stream:stream.write(raw);stream.flush();os.fsync(stream.fileno())
        os.replace(name,str(path))
        directory=os.open(str(path.parent),os.O_RDONLY|os.O_DIRECTORY)
        try:os.fsync(directory)
        finally:os.close(directory)
    finally:
        if os.path.exists(name):os.unlink(name)

def command(action,check=True):
    if action not in ('is-active','stop','start'):raise ValueError('fixed observer actions only')
    p=subprocess.run(['/bin/systemctl',action,SERVICE],capture_output=True,timeout=20)
    if check and p.returncode:raise RuntimeError('observer service operation failed')
    return p.returncode

def probe(raw,config_raw):
    monitor=types.ModuleType('reviewed_warning_candidate')
    exec(compile(raw,'<reviewed-warning-candidate>','exec'),monitor.__dict__)
    config=monitor.validate_config(json.loads(config_raw))
    sample=monitor.fetch_sample(config);current=monitor.evaluate(sample,time.time())
    if not sample or sample.get('warning_schema')!=1 or current['status'] in ('SOURCE_UNAVAILABLE','SOURCE_INVALID','WARNING_POLICY_UNAVAILABLE','OFFLINE','LEGACY_FIRMWARE'):
        raise ValueError('fresh warning-schema telemetry is required')
    return {k:current[k] for k in ('status','battery_warning_active') if k in current}

def observe():return json.loads(read(STATE/'state.json',True))

def upgrade(raw,old_sha,new_sha,unit_sha,apply=False):
    if os.geteuid()!=OWNER_UID:raise ValueError('existing owner-authorized root operator required')
    if not isinstance(raw,bytes) or not 0<len(raw)<=65536 or not all(re.fullmatch('[a-f0-9]{64}',s) for s in (old_sha,new_sha,unit_sha)):
        raise ValueError('bounded exact reviewed source identities required')
    if digest(raw)!=new_sha:raise ValueError('candidate hash mismatch')
    compile(raw,'<reviewed-warning-candidate>','exec')
    for directory in (PROGRAM.parent,STATE,BACKUP.parent):protected_dir(directory)
    before=read(PROGRAM);config=read(CONFIG,True)
    if digest(read(UNIT))!=unit_sha:raise ValueError('existing observer unit changed')
    if digest(before) not in (old_sha,new_sha):raise ValueError('existing observer source changed')
    current=probe(raw,config)
    if digest(before)==new_sha:
        if command('is-active',False)!=0:raise RuntimeError('updated observer not active')
        return {'already_current':True,'updated':False,'source_sha256':new_sha,'current':current}
    if not apply:return {'preflight':True,'source_sha256':new_sha,'current':current}
    # Exclusive private checkpoint, never overwrite a prior failed deployment.
    BACKUP.mkdir(mode=0o700)
    was_active=command('is-active',False)==0
    original_state=None;stopped=False
    try:
        stopped=True;command('stop')
        original_state=read(STATE/'state.json',True) if (STATE/'state.json').exists() else None
        atomic(BACKUP/'monitor.py',before,0o600)
        if original_state is not None:atomic(BACKUP/'state.json',original_state,0o600)
        atomic(BACKUP/'identity.json',json.dumps({'old_sha256':old_sha,'new_sha256':new_sha,'unit_sha256':unit_sha,'config_sha256':digest(config),'was_active':was_active}).encode(),0o600)
        atomic(PROGRAM,raw,0o644)
        started=time.time();command('start')
        until=time.monotonic()+20;latest=None
        while time.monotonic()<until:
            latest=observe()
            if latest.get('observed_at',0)>=started-1 and latest.get('warning_policy_schema')==1:break
            time.sleep(0.5)
        if not latest or latest.get('warning_policy_schema')!=1 or latest.get('observed_at',0)<started-1 or latest['current']['status'] in ('SOURCE_UNAVAILABLE','SOURCE_INVALID','WARNING_POLICY_UNAVAILABLE'):
            raise RuntimeError('fresh updated observer result required')
        if digest(read(PROGRAM))!=new_sha or read(CONFIG,True)!=config or digest(read(UNIT))!=unit_sha or command('is-active',False)!=0:
            raise RuntimeError('observer post-check failed')
        return {'updated':True,'already_current':False,'source_sha256':new_sha,'service_active':True,'configuration_preserved':True,'current':{k:latest['current'][k] for k in ('status','battery_warning_active') if k in latest['current']}}
    except Exception:
        if stopped:
            command('stop',False)
            atomic(PROGRAM,before,0o644)
            if original_state is not None:atomic(STATE/'state.json',original_state,0o600)
            if was_active:command('start')
        raise
