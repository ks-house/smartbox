"""Repair only the existing SmartBox authenticated OTA content handler; no listener/auth/network changes."""
from pathlib import Path
import argparse
import hashlib
import http.client
import importlib.util
import json
import os
import re
import socket
import ssl
import stat
import subprocess
import tempfile
import time

GLOBAL=Path('/etc/nginx/nginx.conf')
SITES=Path('/etc/nginx/sites-enabled')

def render(original,webroot):
    path=str(webroot)
    if not re.fullmatch(r'/volume1/[A-Za-z0-9_./@-]+',path) or '..' in webroot.parts:
        raise ValueError('fixed volume content root required')
    if not all(token in original for token in ['firmware','version','auth_basic','auth_basic_user_file']):
        raise ValueError('existing authenticated SmartBox guard required')
    pattern=r'(?m)^(?P<indent>[ \t]*)proxy_pass[ \t]+http://[0-9.]+:[0-9]+;[ \t]*$'
    if (len(re.findall(pattern,original))!=2 or original.count('location = /version.json {')!=1 or original.count('location = /firmware.bin {')!=1 or original.count('auth_basic_user_file')!=2):
        raise ValueError('two exact authenticated OTA handlers required')
    return re.sub(pattern,lambda x:x['indent']+'root '+path+';\n'+x['indent']+'disable_symlinks on;',original)

def global_candidate(original,sites,portal,copy):
    needle='include sites-enabled/*;'
    if original.count(needle)!=1:raise ValueError('one existing sites include required')
    lines=[]
    for p in sorted(sites.iterdir()):
        if p.is_file():lines.append('include '+str(copy if p.resolve()==portal.resolve() else p)+';')
    if not any(str(copy) in x for x in lines):raise ValueError('portal not selected')
    return original.replace(needle,'\n'.join(lines))

def private_write(path,data,mode=0o600,uid=0,gid=0):
    fd,tmp=tempfile.mkstemp(prefix='.ota-repair-',dir=str(path.parent))
    try:
        os.fchmod(fd,mode);os.fchown(fd,uid,gid)
        with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
        os.replace(tmp,str(path))
    finally:
        if os.path.exists(tmp):os.unlink(tmp)

def checked(args):
    result=subprocess.run(args,capture_output=True,timeout=15)
    if result.returncode:raise RuntimeError('private nginx validation/reload failed')
    return result

def read_config(path):
    info=path.lstat()
    if path.is_symlink() or not stat.S_ISREG(info.st_mode) or info.st_uid!=0 or info.st_mode&0o022 or info.st_size>1048576:
        raise ValueError('trusted existing configuration required')
    return path.read_text()

def bound_artifacts(webroot,baseline):
    old=Path(baseline['backup_directory'])
    out={}
    for name in ('version.json','firmware.bin'):
        current=webroot/name;previous=old/name
        if current.is_symlink() or previous.is_symlink():raise ValueError('regular captured artifacts required')
        out[name]=current.read_bytes()
        if out[name]!=previous.read_bytes():raise ValueError('prior artifact pair changed')
    if (json.loads(out['version.json']).get('version')!=baseline['version'] or
        hashlib.sha256(out['firmware.bin']).hexdigest()!=baseline['sha256']):
        raise ValueError('captured manifest/image identity differs')
    return out

def live_content(expected):
    # Existing trusted loopback route with correct TLS SNI; no credential or peer override.
    context=ssl.create_default_context();end=time.monotonic()+8
    while True:
        try:
            for uri in ('version.json','firmware.bin'):
                c=http.client.HTTPSConnection('tworimpa.synology.me',4443,context=context,timeout=3)
                raw=socket.create_connection(('127.0.0.1',4443),timeout=3)
                c.sock=context.wrap_socket(raw,server_hostname='tworimpa.synology.me')
                try:
                    c.request('GET','/'+uri);r=c.getresponse();body=r.read(len(expected[uri])+1)
                    if r.status!=200 or body!=expected[uri]:raise RuntimeError('live OTA content identity differs')
                finally:c.close()
            return
        except Exception:
            if time.monotonic()>=end:raise
            time.sleep(0.2)

def activate_guard(nginx,guard,original,candidate,info,config,old_config,new_config,config_info,verify):
    if guard.read_bytes()!=original or config.read_bytes()!=old_config:raise ValueError('concurrent policy mutation')
    try:
        private_write(guard,candidate,stat.S_IMODE(info.st_mode),info.st_uid,info.st_gid)
        checked([nginx,'-t','-p','/etc/nginx/','-c',str(GLOBAL)])
        checked([nginx,'-s','reload','-p','/etc/nginx/','-c',str(GLOBAL)])
        verify()
        if config.read_bytes()!=old_config:raise ValueError('concurrent integrity binding mutation')
        private_write(config,new_config,stat.S_IMODE(config_info.st_mode),config_info.st_uid,config_info.st_gid)
    except Exception:
        if guard.read_bytes()!=candidate or config.read_bytes() not in (old_config,new_config):
            raise RuntimeError('concurrent configuration prevents exact rollback')
        private_write(guard,original,stat.S_IMODE(info.st_mode),info.st_uid,info.st_gid)
        if config.read_bytes()!=old_config:private_write(config,old_config,stat.S_IMODE(config_info.st_mode),config_info.st_uid,config_info.st_gid)
        checked([nginx,'-t','-p','/etc/nginx/','-c',str(GLOBAL)])
        checked([nginx,'-s','reload','-p','/etc/nginx/','-c',str(GLOBAL)])
        raise

def fixture(nginx,webroot,work):
    directory=work/'content-fixture';directory.mkdir(mode=0o700)
    sock=directory/'fixture.sock'
    conf=('user http;\npid '+str(directory/'nginx.pid')+';\nerror_log '+str(directory/'error.log')+' warn;\n'
          'events {worker_connections 32;}\nhttp {access_log off; default_type application/octet-stream;\nserver {listen unix:'+str(sock)+';\n'
          'location = /version.json {root '+str(webroot)+';disable_symlinks on;}\n'
          'location = /firmware.bin {root '+str(webroot)+';disable_symlinks on;}\n}}\n')
    path=directory/'fixture.conf';private_write(path,conf.encode())
    checked([nginx,'-t','-p','/etc/nginx/','-c',str(path)])
    process=subprocess.Popen([nginx,'-p','/etc/nginx/','-c',str(path),'-g','daemon off;'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        end=time.monotonic()+4
        while not sock.exists() and time.monotonic()<end:
            if process.poll() is not None:raise RuntimeError('private content fixture exited')
            time.sleep(0.05)
        def request(method,uri,headers=None):
            conn=http.client.HTTPConnection('localhost',timeout=3);conn.sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);conn.sock.settimeout(3);conn.sock.connect(str(sock))
            conn.request(method,uri,headers=headers or {});r=conn.getresponse();data=r.read(65537)
            result=(r.status,dict(r.getheaders()),data);conn.close();return result
        status,headers,data=request('GET','/version.json');assert status==200 and json.loads(data).get('version')
        status,headers,data=request('HEAD','/firmware.bin');assert status==200 and int(headers.get('Content-Length','0'))==(webroot/'firmware.bin').stat().st_size
        status,headers,data=request('GET','/firmware.bin',{'Range':'bytes=0-0'});assert status==206 and data==b'\xe9'
        return 3
    finally:
        process.terminate()
        try:process.wait(timeout=4)
        except subprocess.TimeoutExpired:process.kill();process.wait(timeout=4)

def repair(expected_guard=None,apply=False):
    if os.geteuid()!=0:raise ValueError('owner-authorized root operator required')
    sp=importlib.util.spec_from_file_location('broker_ops','/usr/local/lib/smart-gatekeeper-broker-ops/lib/activate.py');docker=importlib.util.module_from_spec(sp);sp.loader.exec_module(docker)
    c=docker.inspect('SmartBox_OTA');identifier=c['Id']
    if not c['State']['Running'] or not re.fullmatch(r'[a-f0-9]{64}',identifier):raise ValueError('existing SmartBox static service required')
    mounts=[x for x in c['Mounts'] if x['Destination']=='/usr/share/nginx/html']
    if len(mounts)!=1:raise ValueError('one existing content mount required')
    webroot=Path(mounts[0]['Source']);baseline=json.loads(Path('/usr/local/lib/smart-gatekeeper-maintenance/state/smartbox-battery-publication-baseline.json').read_text())
    if str(webroot)!=baseline['source_directory'] or webroot.is_symlink():raise ValueError('backed-up content source required')
    expected_content=bound_artifacts(webroot,baseline)
    services=list(Path('/etc/nginx/conf.d').glob('.service.*.Docker-'+identifier+'-80.conf'))
    if len(services)!=1:raise ValueError('one generated WebStation service required')
    service=services[0];guarddir=Path('/usr/local/etc/nginx/conf.d')/('Docker-'+identifier+'-80');guard=guarddir/'user.conf.smartbox_ota_origin_guard'
    original=read_config(guard);oldsha=hashlib.sha256(original.encode()).hexdigest()
    if apply and (not expected_guard or expected_guard!=oldsha):raise ValueError('exact preflight guard digest required')
    candidate=render(original,webroot);service_text=read_config(service.resolve())
    config=Path('/usr/local/lib/smart-gatekeeper-broker-ops/config.json');config_text=read_config(config);config_bytes=config_text.encode();binding=json.loads(config_text)
    if binding.get('origin_guard_hashes',{}).get(str(guard))!=oldsha:raise ValueError('guard integrity binding differs')
    binding['origin_guard_hashes'][str(guard)]=hashlib.sha256(candidate.encode()).hexdigest()
    new_config=json.dumps(binding,sort_keys=True).encode()
    hook='include '+str(guarddir)+'/user.conf*;'
    if service_text.count(hook)!=1:raise ValueError('persistent hook changed')
    needle='include conf.d/'+service.name+'*;'
    portals=[p.resolve() for p in SITES.iterdir() if p.is_file() and needle in p.read_text()]
    if len(set(portals))!=1:raise ValueError('one portal include required')
    portal=portals[0];portal_text=read_config(portal);global_text=read_config(GLOBAL.resolve())
    base=Path('/volume1/smartbox-operations-private/ota-route-repair');base.mkdir(mode=0o700,exist_ok=True)
    info=base.lstat()
    if base.is_symlink() or info.st_uid!=0 or stat.S_IMODE(info.st_mode)!=0o700:raise ValueError('private evidence directory required')
    work=Path(tempfile.mkdtemp(prefix='attempt-',dir=str(base)));os.chmod(str(work),0o700)
    private_write(work/'guard.original',original.encode());private_write(work/'guard.candidate',candidate.encode())
    includes=[]
    for p in sorted(guarddir.glob('user.conf*')):
        if p.is_file():includes.append('include '+str(work/'guard.candidate' if p==guard else p)+';')
    private_write(work/'service.candidate',service_text.replace(hook,'\n'.join(includes)).encode())
    if portal_text.count(needle)!=1:raise ValueError('ambiguous service selection')
    private_write(work/'portal.candidate',portal_text.replace(needle,'include '+str(work/'service.candidate')+';').encode())
    full=global_candidate(global_text,SITES,portal,work/'portal.candidate')
    nginx=next((p for p in ['/usr/syno/sbin/nginx','/usr/bin/nginx','/bin/nginx'] if Path(p).is_file()),None)
    if not nginx:raise ValueError('existing nginx binary required')
    fd,name=tempfile.mkstemp(prefix='.smartbox-ota-validation-',dir='/etc/nginx')
    try:
        with os.fdopen(fd,'w') as f:f.write(full)
        checked([nginx,'-t','-p','/etc/nginx/','-c',name])
    finally:Path(name).unlink()
    cases=fixture(nginx,webroot,work)
    out={'full_candidate_valid':True,'http_worker_content_cases':cases,'guard_before_sha256':oldsha,'guard_after_sha256':hashlib.sha256(candidate.encode()).hexdigest(),'auth_and_access_bytes_preserved':True,'new_listener_or_credentials':False,'applied':False,'rollback':False,'backup_directory':str(work)}
    if apply:
        if bound_artifacts(webroot,baseline)!=expected_content:raise ValueError('artifact changed after validation')
        private_write(work/'gateway.original.json',config_bytes)
        activate_guard(nginx,guard,original.encode(),candidate.encode(),guard.stat(),config,config_bytes,new_config,config.stat(),lambda:(live_content(expected_content),docker.ready()))
        out['applied']=True;out['live_tls_content_verified']=True;out['external_authenticated_check_pending']=True
    private_write(work/'result.json',json.dumps(out).encode())
    docker.atomic(Path('/usr/local/lib/smart-gatekeeper-maintenance/state/smartbox-static-origin-repair.json'),json.dumps(out))
    return out

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--apply',action='store_true');parser.add_argument('--expected-guard');a=parser.parse_args()
    try:print(json.dumps(repair(a.expected_guard,a.apply)))
    except Exception as e:print(json.dumps({'applied':False,'error_type':type(e).__name__}));raise SystemExit(1)
if __name__=='__main__':main()
