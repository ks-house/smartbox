"""Single owner-authorized legacy-to-power-policy transition; never a general OTA override.

The deployed >=12.4V automatic OTA gate and >=12.4V/300s motor recovery stay unchanged.
This operator applies only to the captured e0323a0 legacy whose BATTERY_LOW_SHUTDOWN
persists lid-open and whose next reviewed boot restores isolated protection.
Its 12.0V unloaded floor is an operator admission margin, not SOC/rail measurement.
"""
import json
import math
import os
import re

LEGACY_VERSION='0.1.0-ge0323a0'

def number(value):
    if type(value) not in (int,float) or not math.isfinite(value):raise ValueError('finite numeric evidence required')
    return value

def admit(rows,now,legacy_version):
    if legacy_version!=LEGACY_VERSION or len(rows)<4:raise ValueError('captured legacy and four fresh observations required')
    rows=rows[-4:];now=number(now)
    for row in rows:
        if not isinstance(row,dict) or row.get('state')!='BATTERY_LOW_SHUTDOWN' or row.get('ota_pending') is not False or row.get('power_schema') is not None:
            raise ValueError('isolated legacy with no pending OTA required')
        if not 12.0<=number(row.get('voltage'))<=16.0 or abs(number(row.get('current_ma')))>2.0:
            raise ValueError('stable unloaded first-transition margin required')
        if number(row.get('uptime_ms'))<0:raise ValueError('valid live uptime required')
        number(row.get('at'))
    if not 85<=rows[-1]['at']-rows[0]['at']<=110 or not 0<=now-rows[-1]['at']<=3:
        raise ValueError('current continuous observation window required')
    for left,right in zip(rows,rows[1:]):
        gap=right['at']-left['at'];uptime=(right['uptime_ms']-left['uptime_ms'])/1000.0
        if not 20<=gap<=35 or abs(gap-uptime)>3:raise ValueError('gap/reboot evidence is not admissible')
    volts=[r['voltage'] for r in rows]
    if max(volts)-min(volts)>0.05 or volts[-1]<volts[0]-0.02:
        raise ValueError('voltage stability required')
    if any(r['voltage']==12.2 and r['current_ma']==0 for r in rows):
        raise ValueError('legacy fabricated sensor fallback rejected')
    return {'accepted':True,'legacy_version':legacy_version,'min_voltage':min(volts),'max_voltage':max(volts),
            'window_seconds':rows[-1]['at']-rows[0]['at'],'last_uptime_ms':rows[-1]['uptime_ms'],
            'new_firmware_power_thresholds_changed':False,'actuator_commands':0}

def validate_candidate(meta):
    if not isinstance(meta,dict):raise ValueError('exact reviewed artifact required')
    commit=meta.get('commit','');version=meta.get('version','')
    if not re.fullmatch(r'[a-f0-9]{40}',commit) or not re.fullmatch(r'0\.2\.0-g[a-f0-9]{7,40}',version):
        raise ValueError('captured new power-policy build required')
    if not commit.startswith(version.split('-g')[1]) or not re.fullmatch(r'[a-f0-9]{64}',meta.get('sha256','')):
        raise ValueError('bound artifact identity required')
    if type(meta.get('size')) is not int or not 100000<meta['size']<=6553600:raise ValueError('bounded ESP32-C6 OTA image required')

def trigger_once(client,intent_path,rows,now,legacy_version,candidate):
    validate_candidate(candidate);proof=admit(rows,now,legacy_version)
    intent={'state':'reserved_before_single_publish','candidate':candidate,'admission':proof,'at':now,
            'command':'trigger_ota','qos':0,'retain':False,'automatic_resend':False}
    # A lost connection/uncertain outcome leaves the reservation. Never delete it to retry.
    fd=os.open(str(intent_path),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'w') as f:json.dump(intent,f);f.flush();os.fsync(f.fileno())
    parent=os.open(str(intent_path.parent),os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    try:os.fsync(parent)
    finally:os.close(parent)
    result=client.publish('smartbox/command','{"command":"trigger_ota"}',qos=0,retain=False)
    if result.rc!=0:raise RuntimeError('uncertain first transition; reservation retained and no resend permitted')
    return intent


def accept_boot(rows,status,sent_at,old_uptime,version,now):
    """Accept only a fresh consecutive pair from one expected new boot, never PUBACK."""
    try:
        if len(rows)<2 or status.get('status')!='online' or status.get('version')!=version or status.get('retained') is not False:
            return False
        left,right=rows[-2:];start=number(sent_at);now=number(now);old=number(old_uptime)
        if not start<number(status['at'])<=number(left['at'])<number(right['at'])<=now or now-right['at']>10:
            return False
        for row in (left,right):
            if row.get('firmware')!=version or row.get('power_schema')!=1 or row.get('power_sensor_valid')!=1:return False
            if not 0<=number(row['uptime_ms'])<old:return False
        elapsed=right['at']-left['at'];uptime=(right['uptime_ms']-left['uptime_ms'])/1000.0
        return 30<=elapsed<=400 and uptime>0 and abs(elapsed-uptime)<=5
    except (ValueError,KeyError,TypeError):return False
