#!/usr/bin/env python3
"""One bounded, explicitly authorized OTA bootstrap; sends no device commands."""
import hashlib
import fcntl
import http.client
import json
import os
from pathlib import Path
import re
import signal
import socket
import ssl
import time
try:
    from . import install_ota_origin_guard as guard
    from . import authenticated_ota_origin as auth
    from . import observe_ota_origin as observer
except ImportError:
    import install_ota_origin_guard as guard
    import authenticated_ota_origin as auth
    import observe_ota_origin as observer


def local_read(origin, uri, limit):
    connection=http.client.HTTPSConnection(origin["hostname"],origin["port"],timeout=15)
    connection.sock=ssl.create_default_context().wrap_socket(
        socket.create_connection(("127.0.0.1",origin["port"]),timeout=15),
        server_hostname=origin["hostname"])
    try:
        connection.request("GET",uri)
        response=connection.getresponse()
        if response.status!=200:raise RuntimeError("protected local artifact read failed")
        data=response.read(limit+1)
        if len(data)>limit:raise ValueError("artifact size budget exceeded")
        return data
    finally:connection.close()


def verify_artifact(origin, identity, command):
    version=command.get("expected_version","");digest=command.get("expected_sha256","")
    if not re.fullmatch(r"[A-Za-z0-9.+_-]{1,96}",version) or not re.fullmatch(r"[a-f0-9]{64}",digest):
        raise ValueError("exact reviewed artifact binding required")
    manifest=json.loads(local_read(origin,"/version.json",65536))
    if manifest.get("version")!=version:raise ValueError("published version changed")
    image=local_read(origin,"/firmware.bin",8388608)
    if hashlib.sha256(image).hexdigest()!=digest:raise ValueError("published firmware changed")
    for required in (version.encode(),identity["username"].encode(),identity["password"].encode(),
                     b"HTTPS authentication client schema=1"):
        if required not in image:raise ValueError("image lacks the bound authenticated client")
    return version,digest,len(image)


def observation_config(logfile):
    text=observer.http_config().replace("smartbox_ota_trace","smartbox_ota_auth_trace")
    text=text.replace('"agent":"$http_user_agent"}',
        '"agent":"$http_user_agent","user":"$remote_user","realm":"$smartbox_ota_auth_realm","bytes":$body_bytes_sent}')
    location="access_log "+str(logfile)+" smartbox_ota_auth_trace if=$smartbox_ota_auth_trace_enabled;\n"
    return text,location


def matching_transfer(row, image_bytes):
    return (row.get("uri")=="/firmware.bin" and row.get("method")=="GET"
            and row.get("status")==200 and row.get("bytes")==image_bytes
            and str(row.get("agent","")).lower().startswith("esp32"))


def authenticated_check(row, identity, peer):
    return (row.get("peer")==peer and row.get("uri")=="/version.json"
            and row.get("method")=="GET" and row.get("status")==200
            and row.get("user")==identity["username"] and row.get("realm")!="off"
            and str(row.get("agent","")).lower().startswith("esp32"))


def remove_peer(path, content):
    if not path.exists() and not path.is_symlink():return
    if path.is_symlink() or path.stat().st_uid!=0 or path.read_bytes()!=content.encode():
        raise RuntimeError("concurrent bootstrap peer change; refusing unsafe cleanup")
    path.unlink()
    guard.run([guard.NGINX,"-t"]);guard.run([guard.NGINX,"-s","reload"])


def watchdog_cleanup(files, time_path):
    targets=list(files)
    if time_path.exists() and not time_path.is_symlink():
        content=guard.regular(time_path).read_text()
        match=re.match(r'"~\^\(\?:(\d{10})',content)
        if match and auth.bootstrap_time_entry(int(match.group(1)))==content:
            targets.append((time_path,content))
    observer.remove_exact(targets)


def verify_policy(state):
    for name,digest in state["installed_file_hashes"].items():
        if guard.digest(guard.regular(Path(name)).read_bytes())!=digest:
            raise ValueError("authenticated origin changed while waiting")
    if guard.digest(guard.regular(auth.AUTH_FILE).read_bytes())!=state["hashed_credential_file_sha256"]:
        raise ValueError("bound hashed credential changed")
    current=json.loads(guard.regular(observer.updater.OPS_CONFIG).read_text())
    if current["origin_guard_hashes"]!=state["installed_file_hashes"]:
        raise ValueError("authenticated gateway binding changed")


def mqtt_peer_present(peer):
    expected=socket.inet_aton(peer)[::-1].hex().upper()
    for line in Path("/proc/net/tcp").read_text().splitlines()[1:]:
        fields=line.split()
        if fields[3]!="01":continue
        local_port=int(fields[1].split(":")[1],16)
        remote=fields[2].split(":")[0]
        if local_port==4883 and remote==expected:return True
    return False


def start_session(origin,work,owner,identity,peer,response,initial,restore):
    lock=os.open("/volume1/smart-gatekeeper-broker-private/ota-auth-bootstrap.lock",
                 os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
    try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except OSError:
        os.close(lock);raise ValueError("another bounded bootstrap is active")
    runtime=auth.RUNTIME
    if not runtime.exists():runtime.mkdir(mode=0o700)
    if runtime.is_symlink() or runtime.stat().st_uid!=0 or runtime.stat().st_mode&0o077:
        raise ValueError("private volatile bootstrap directory required")
    if any(runtime.iterdir()):raise ValueError("another volatile bootstrap exists")
    logfile=work/"auth.requests.jsonl";guard.create(logfile,"")
    http,location=observation_config(logfile)
    files=[(runtime/"http.observe.trace.conf",http),
           (runtime/"location.observe.trace.conf",location)]
    peer_path=runtime/"peer.once.conf";peer_content=auth.bootstrap_entry(peer)
    time_path=runtime/"time.once.conf"
    state={**initial,"state":"waiting_for_agent","wait_seconds":600,
           "bootstrap_seconds":300,"capture_ready":False,"trace_cleaned":True,
           "public_bootstrap_active":False,"requests":[]}
    observer.report(response,owner,state)
    pid=os.fork()
    if pid==0:
        os.setsid()
        fd=os.open(str(work/"bootstrap.worker.log"),os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600)
        os.dup2(fd,1);os.dup2(fd,2);os.close(fd)
        fd=os.open("/dev/null",os.O_RDONLY);os.dup2(fd,0);os.close(fd)
        state["pid"]=os.getpid()
        installed=False;watchdog=None
        all_files=[*files,(peer_path,peer_content)]
        try:
            watchdog=os.fork()
            if watchdog==0:
                try:
                    time.sleep(600+300+90)
                    watchdog_cleanup(all_files,time_path)
                finally:os._exit(0)
            state["watchdog_pid"]=watchdog
            observer.report(response,owner,state)
            stop=[False]
            signal.signal(signal.SIGTERM,lambda *_:stop.__setitem__(0,True))
            end=time.monotonic()+600;command={}
            while time.monotonic()<end and not stop[0]:
                raw=response.read_bytes()[:8193]
                if len(raw)<=8192:
                    try:command=json.loads(raw)
                    except ValueError:command={}
                    if isinstance(command,dict) and (command.get("activate") is True or command.get("rollback") is True):
                        break
                time.sleep(0.5)
            if isinstance(command,dict) and command.get("rollback") is True:
                restore();state.update(state="rolled_back",policy_rolled_back=True)
            elif not isinstance(command,dict) or command.get("activate") is not True or stop[0]:
                state["state"]="expired"
            else:
                verify_policy(state)
                if state["public_bootstrap_authorized"] and not mqtt_peer_present(peer):
                    raise ValueError("reviewed field peer is no longer connected")
                version,digest,image_bytes=verify_artifact(origin,identity,command)
                state.update(expected_version=version,expected_sha256=digest,image_bytes=image_bytes)
                guard.read_plan_from_sources(origin)
                for path,content in all_files:
                    if path.exists() or path.is_symlink():raise ValueError("runtime file appeared")
                installed=True;state["trace_cleaned"]=False
                opened_epoch=int(time.time())
                time_content=auth.bootstrap_time_entry(opened_epoch)
                all_files.append((time_path,time_content))
                state["bootstrap_request_expires_at_epoch"]=opened_epoch+300
                written=[]
                try:
                    for path,content in files:
                        guard.create(path,content);written.append((path,content))
                    if state["public_bootstrap_authorized"]:
                        guard.create(time_path,time_content);written.append((time_path,time_content))
                        guard.create(peer_path,peer_content);written.append((peer_path,peer_content))
                    guard.run([guard.NGINX,"-t"]);guard.run([guard.NGINX,"-s","reload"])
                    state["state"]="verifying_capture";observer.report(response,owner,state)
                    state["capture_check"]=observer.capture_self_check(origin,logfile)
                    state.update(capture_ready=True,state="active",
                                 public_bootstrap_active=state["public_bootstrap_authorized"])
                except Exception:
                    observer.remove_exact(written);installed=False;state["trace_cleaned"]=True;raise
                deadline=time.monotonic()+max(0,opened_epoch+300-time.time())
                transfer_seen=False;authenticated_seen=False
                while time.monotonic()<deadline and not stop[0]:
                    rows=observer.read_records(logfile)
                    state["requests"]=observer.summary(rows,[])
                    if not transfer_seen and any(matching_transfer(row,image_bytes) for row in rows):
                        transfer_seen=True
                        remove_peer(peer_path,peer_content)
                        state.update(state="awaiting_authenticated_check",firmware_transfer_complete=True,
                                     public_bootstrap_active=False,bootstrap_closed_after_transfer=True)
                    if transfer_seen and any(authenticated_check(row,identity,peer) for row in rows):
                        authenticated_seen=True
                        state.update(state="authenticated_check_verified",
                                     authenticated_client_check_verified=True)
                        break
                    observer.report(response,owner,state)
                    time.sleep(0.5)
                if not authenticated_seen:state["state"]="observation_finished"
                state["firmware_transfer_complete"]=transfer_seen
                state["authenticated_client_check_verified"]=authenticated_seen
        except Exception as error:
            state.update(failed_stage=state["state"],state="failed",error_type=type(error).__name__)
            try:state["requests"]=observer.summary(observer.read_records(logfile),[])
            except OSError:pass
            guard.create(work/"bootstrap.error.json",json.dumps({
                "error_type":type(error).__name__,"message":str(error),"failed_query":guard.FAILED_QUERY}))
        finally:
            if installed:
                try:
                    observer.remove_exact(all_files)
                    state.update(trace_cleaned=True,public_bootstrap_active=False)
                except Exception as error:
                    state.update(cleanup_error=type(error).__name__,trace_cleaned=False)
            observer.report(response,owner,state)
            if watchdog and state["trace_cleaned"]:
                try:os.kill(watchdog,signal.SIGTERM)
                except ProcessLookupError:pass
            os._exit(0)
    print(json.dumps({"authenticated_ota_policy_installed":True,"bootstrap_session_ready":True,
                     "public_bootstrap_authorized":state["public_bootstrap_authorized"],
                     "public_bootstrap_active":False,"pid":pid,"wait_seconds":600,
                     "bootstrap_seconds":300,"general_shell":False}))
