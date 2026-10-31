"""Read-only SmartBox power observer. No MQTT/device/control publishing."""
import argparse
import csv
import datetime
import io
import json
import math
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request
import uuid

ROOT=Path('/usr/local/lib/smartbox-power-monitor')
NOTIFIER='/usr/syno/bin/synodsmnotify'
STATES={0:'NORMAL',1:'LOW_BATTERY',2:'ENERGY_SAVING',3:'BATTERY_PROTECTED',4:'RECOVERY_WAIT',5:'POWER_SENSOR_FAULT',6:'INITIALIZING'}
FIELDS={'power_schema':'schema','power_sensor_valid':'valid','power_state_code':'state',
 'power_report_interval_s':'interval_s','power_sample_age_ms':'sample_age_ms','battery_v':'voltage'}

class SourceResponseError(RuntimeError):pass
class SourceFormatError(ValueError):pass

def evaluate(sample,now,source_ok=True):
    value={'status':'SOURCE_UNAVAILABLE','severity':'warning','voltage_v':None}
    if not source_ok:return value
    if sample is None:value['status']='OFFLINE';return value
    try:
        at=float(sample['at']);age=float(now)-at
        if not math.isfinite(age) or age < -5:raise ValueError('receipt clock')
        value['sample_age_s']=max(0,round(age,1))
        if sample.get('schema')!=1:value['status']='LEGACY_FIRMWARE';return value
        interval=sample['interval_s'];code=sample['state']
        if interval not in (30,60,120,300) or code not in STATES:raise ValueError('closed schema')
        value['report_interval_s']=interval
        if age>2*interval+30:value['status']='OFFLINE';return value
        voltage=sample.get('voltage')
        age_ms=float(sample.get('sample_age_ms',999999))
        if sample.get('valid')!=1 or not math.isfinite(age_ms) or not 0<=age_ms<=2000 or not isinstance(voltage,(int,float)) or not math.isfinite(voltage) or not 0<voltage<=16:
            value['status']='POWER_SENSOR_FAULT';return value
        value['voltage_v']=round(voltage,3);value['status']=STATES[code]
        value['severity']='info' if code in (0,4,6) else 'warning'
        return value
    except (ValueError,TypeError,KeyError,OverflowError):
        value['status']='SOURCE_INVALID';return value

def advance(previous,current,now):
    state=dict(previous);status=current['status'];active=state.get('active','NORMAL')
    if state.get('candidate')!=status or now<state.get('candidate_since',now):
        state.update(candidate=status,candidate_since=now)
    event=None
    dwell=60 if status in ('LOW_BATTERY','ENERGY_SAVING') else 0
    if status!=active and now-state['candidate_since']>=dwell:
        state['active']=status
        event={'id':uuid.uuid4().hex[:16],'status':'RECOVERED' if status=='NORMAL' else status,
               'severity':current['severity'],'at':int(now),'voltage_v':current['voltage_v']}
    state['current']=current;state['observed_at']=now
    return state,event

def epoch(value):
    value=re.sub(r'(\.\d{6})\d+',r'\1',value).replace('Z','+00:00')
    parsed=datetime.datetime.fromisoformat(value)
    if parsed.tzinfo is None:raise ValueError('timezone required')
    return parsed.timestamp()

def parse_flux(text):
    values={};header=None
    for cells in csv.reader(io.StringIO(text)):
        if not cells or not any(cells) or cells[0].startswith('#'):continue
        if 'error' in cells and 'reference' in cells:
            raise SourceResponseError('query error record')
        if all(k in cells for k in ('_time','_field','_value')):
            header=cells;continue
        if header is None or len(cells)!=len(header):raise SourceFormatError('invalid query record')
        row=dict(zip(header,cells));field=row.get('_field')
        if field not in FIELDS:continue
        try:values[FIELDS[field]]=(epoch(row['_time']),float(row['_value']))
        except (ValueError,KeyError,TypeError,OverflowError):raise SourceFormatError('invalid sample record')
    if not values:return None
    if 'schema' not in values:
        return {'schema':0,'at':max(t for t,v in values.values())}
    at=values['schema'][0]
    if 'voltage' in values and values['voltage'][0]>at+0.001:
        return {'schema':0,'at':values['voltage'][0]}
    required=('schema','state','valid','interval_s','sample_age_ms')
    if any(k not in values or abs(values[k][0]-at)>0.001 for k in required):
        raise SourceFormatError('incomplete or mixed telemetry cohort')
    sample={k:v for k,(t,v) in values.items() if abs(t-at)<=0.001};sample['at']=at
    return sample

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):return None

def fetch_sample(config):
    filters=' or '.join('r._field == '+json.dumps(k) for k in FIELDS)
    flux=('from(bucket:'+json.dumps(config['bucket'])+') |> range(start:-2h) '
          '|> filter(fn:(r)=> r.topic == "smartbox/telemetry") '
          '|> filter(fn:(r)=> '+filters+') |> group(columns:["_field"]) |> last() '
          '|> keep(columns:["_time","_field","_value"])')
    url='http://127.0.0.1:4086/api/v2/query?org='+urllib.parse.quote(config['org'],safe='')
    req=urllib.request.Request(url,data=flux.encode(),headers={'Authorization':'Token '+config['token'],
        'Content-Type':'application/vnd.flux','Accept':'application/csv'},method='POST')
    with urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect()).open(req,timeout=10) as response:data=response.read(65537)
    if len(data)>65536:raise ValueError('query response bound')
    return parse_flux(data.decode('utf-8'))

def regular(path):
    info=path.lstat()
    if path.is_symlink() or not stat.S_ISREG(info.st_mode) or info.st_uid!=0 or stat.S_IMODE(info.st_mode)&0o077 or info.st_size>262144:
        raise ValueError('private bounded regular file required')
    return path.read_text()

def atomic(path,value):
    fd,tmp=tempfile.mkstemp(prefix='.power-',dir=str(path.parent))
    try:
        os.fchmod(fd,0o600)
        with os.fdopen(fd,'w',encoding='utf-8') as f:json.dump(value,f,ensure_ascii=False,allow_nan=False);f.flush();os.fsync(f.fileno())
        os.replace(tmp,str(path))
    finally:
        if os.path.exists(tmp):os.unlink(tmp)

MESSAGES={'LOW_BATTERY':'배터리 전압이 낮습니다. 충전 상태를 확인해 주세요.',
 'ENERGY_SAVING':'배터리 절전 단계입니다. 감지는 유지하며 전송 빈도를 줄입니다.',
 'BATTERY_PROTECTED':'저전압 보호로 동작을 제한합니다. 충전 회복 후 자동 재개를 기다립니다.',
 'RECOVERY_WAIT':'충전 회복 전압의 안정성을 확인하고 있습니다.',
 'POWER_SENSOR_FAULT':'전압·전류 측정이 유효하지 않습니다. 정상 전압으로 간주하지 않습니다.',
 'OFFLINE':'예정된 전송 주기를 넘겨 새 자료가 없습니다. 마지막 전압을 현재 상태로 보지 마세요.',
 'SOURCE_UNAVAILABLE':'NAS 수집 경로에 접속할 수 없습니다. 배터리 장애와 구분해 확인해 주세요.',
 'SOURCE_INVALID':'수집 자료의 시각 또는 형식이 올바르지 않습니다.',
 'LEGACY_FIRMWARE':'이 펌웨어에는 배터리 측정 유효성·보호 단계 정보가 없습니다.',
 'INITIALIZING':'배터리 상태를 초기 확인하고 있습니다.',
 'RECOVERED':'SmartBox 감시 상태가 정상으로 돌아왔습니다. 최신 전압·자료 수신 상태를 확인해 주세요.'}

def notify_owner(event,recipient):
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,32}',recipient) or event['status'] not in MESSAGES or not re.fullmatch(r'[a-f0-9]{16}',event['id']):raise ValueError('closed notification')
    message='SmartBox 배터리 감시\n'+MESSAGES[event['status']]
    result=subprocess.run([NOTIFIER,'-t','smartbox-power-'+event['id'],'-l',
       'info' if event['severity']=='info' else 'warn',recipient,'DSMSupportFormCustomMessage',
       json.dumps({'%CUSTOM_MSG%':message},ensure_ascii=False)],capture_output=True,timeout=5)
    return result.returncode==0

def cycle(config,directory,now=None,fetch=fetch_sample,notifier=notify_owner):
    now=time.time() if now is None else now
    previous=json.loads(regular(directory/'state.json')) if (directory/'state.json').exists() else {}
    try:sample=fetch(config);current=evaluate(sample,now)
    except SourceFormatError:current={'status':'SOURCE_INVALID','severity':'warning','voltage_v':None}
    except Exception:current=evaluate(None,now,source_ok=False)
    state,event=advance(previous,current,now)
    pending=state.get('pending',[])
    if event:
        pending.append(event)
        history=json.loads(regular(directory/'events.json')) if (directory/'events.json').exists() else []
        atomic(directory/'events.json',(history+[event])[-50:])
    state['pending']=pending[-16:];atomic(directory/'state.json',state)
    if state['pending']:
        try:accepted=notifier(state['pending'][0],config['recipient'])
        except Exception:accepted=False
        if accepted:
            state['pending'].pop(0);state['last_notification_accepted_at']=now;atomic(directory/'state.json',state)
    return state

def validate_config(config):
    if set(config)!={'org','bucket','token','recipient','state_directory','interval_seconds'}:raise ValueError('closed configuration')
    for key in ('org','bucket','token','recipient'):
        value=config[key]
        if not isinstance(value,str) or not 1<=len(value)<=2048 or '\n' in value or '\r' in value:raise ValueError('bounded configuration')
    if config['interval_seconds']!=60 or config['state_directory']!='/volume1/smartbox-operations-private/power-monitor':raise ValueError('fixed observer binding')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,32}',config['recipient']):raise ValueError('owner binding')
    return config

def main():
    import fcntl
    parser=argparse.ArgumentParser();parser.add_argument('--once',action='store_true');parser.add_argument('--observe-only',action='store_true');args=parser.parse_args()
    if os.geteuid()!=0:raise ValueError('root-owned operator required')
    config=validate_config(json.loads(regular(ROOT/'config.json')))
    directory=Path(config['state_directory']);info=directory.lstat()
    if not stat.S_ISDIR(info.st_mode) or directory.is_symlink() or info.st_uid!=0 or stat.S_IMODE(info.st_mode)!=0o700:raise ValueError('private state required')
    if args.observe_only:
        try:value=evaluate(fetch_sample(config),time.time())
        except Exception:value=evaluate(None,time.time(),source_ok=False)
        print(json.dumps(value));return
    fd=os.open(str(directory/'observer.lock'),os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
    try:
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        while True:
            state=cycle(config,directory)
            if args.once:print(json.dumps({'status':state['current']['status'],'pending':len(state['pending'])}));return
            time.sleep(config['interval_seconds'])
    finally:os.close(fd)
if __name__=='__main__':main()
