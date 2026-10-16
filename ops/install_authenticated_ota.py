#!/usr/bin/env python3
"""Install a pinned authenticated OTA policy; public bootstrap needs explicit consent."""
import argparse
from datetime import datetime, timezone
import grp
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import stat
import tempfile
import time
try:
    from . import install_ota_origin_guard as guard
    from . import update_ota_origin_guard as updater
    from . import authenticated_ota_origin as auth
except ImportError:
    import install_ota_origin_guard as guard
    import update_ota_origin_guard as updater
    import authenticated_ota_origin as auth


class RollbackUnconfirmed(RuntimeError):
    pass


def read_identity(path, expected_sha256, owner):
    if not re.fullmatch(r"/tmp/sgk-ota-auth-stage-[a-z0-9-]{8,64}/identity.json", str(path)):
        raise ValueError("fixed private credential input required")
    parent = path.parent
    info = parent.stat()
    if parent.is_symlink() or info.st_uid != owner.pw_uid or info.st_mode & 0o077:
        raise ValueError("private operator input directory required")
    fd = os.open(str(path), os.O_RDONLY|os.O_NOFOLLOW)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != owner.pw_uid or info.st_mode & 0o077:
            raise ValueError("private operator-owned input required")
        with os.fdopen(fd,"rb",closefd=False) as stream:
            raw=stream.read(4097)
    finally:
        os.close(fd)
    if len(raw)>4096 or guard.digest(raw)!=expected_sha256:
        raise ValueError("captured credential input digest mismatch")
    return auth.credentials(raw), raw


def validate_full(origin, work, http_text, server_text):
    """Replace only the pinned guard in the whole original-prefix nginx tree."""
    source={k:guard.regular(Path(v["path"])).read_text() for k,v in origin["sources"].items()}
    geo=work/"auth.http.candidate.conf";server=work/"auth.server.candidate.conf"
    guard.create(geo,http_text);guard.create(server,server_text)
    service=Path(origin["sources"]["service"]["path"])
    portal=Path(origin["sources"]["portal"]["path"])
    old_guard=Path(origin["guard_directory"])/"user.conf.smartbox_ota_origin_guard"
    user_files=sorted(old_guard.parent.glob("user.conf*"))
    if user_files.count(old_guard)!=1:
        raise ValueError("installed path guard must be present exactly once")
    user_sources={p:guard.regular(p).read_bytes() for p in user_files}
    hook="include "+origin["guard_directory"]+"/user.conf*;"
    if source["service"].count(hook)!=1:
        raise ValueError("pinned service hook changed")
    includes="\n".join("include "+str(server if p==old_guard else p)+";" for p in user_files)
    service_copy=work/"auth.service.candidate.conf"
    guard.create(service_copy,source["service"].replace(hook,includes))
    marker="include conf.d/"+service.name+"*;"
    if source["portal"].count(marker)!=1:
        raise ValueError("pinned portal hook changed")
    portal_copy=work/"auth.portal.candidate.conf"
    guard.create(portal_copy,source["portal"].replace(marker,"include "+str(service_copy)+";"))
    global_text=updater.expand_http_includes(source["global"],
        sorted(guard.HTTP_INCLUDE.parent.glob("http.*.conf")),guard.HTTP_INCLUDE,geo)
    marker="include sites-enabled/*;"
    if global_text.count(marker)!=1:raise ValueError("pinned site inventory changed")
    lines=[]
    for p in sorted(Path("/etc/nginx/sites-enabled").iterdir()):
        lines.append("include "+str(portal_copy if p.resolve()==portal.resolve() else p)+";")
    global_text=global_text.replace(marker,"\n".join(lines))
    fd,name=tempfile.mkstemp(prefix=".ota-auth-validation-",dir="/etc/nginx")
    try:
        with os.fdopen(fd,"w") as stream:stream.write(global_text)
        guard.run([guard.NGINX,"-t","-p","/etc/nginx/","-c",name])
    finally:
        Path(name).unlink()
    return user_sources


def activate(origin, originals, infos, config_bytes, config_info, new_files):
    """Rebind the gateway only after reload/local checks; exact rollback on failure."""
    updated=json.loads(config_bytes)
    updated["origin_guard_hashes"]={str(p):guard.digest(data) for p,data in new_files.items()}
    new_config=json.dumps(updated,sort_keys=True).encode()
    changed=[]
    try:
        for path,data in new_files.items():
            if path.read_bytes()!=originals[path]:raise ValueError("guard changed during activation")
            updater.replace(path,data,infos[path]);changed.append(path)
        guard.run([guard.NGINX,"-t"]);guard.run([guard.NGINX,"-s","reload"])
        time.sleep(0.5)
        for uri in ("/version.json","/firmware.bin"):guard.head_local(origin,uri)
        if updater.OPS_CONFIG.read_bytes()!=config_bytes:
            raise ValueError("gateway binding changed during activation")
        updater.replace(updater.OPS_CONFIG,new_config,config_info)
    except Exception:
        # Restore the path guard before the HTTP variables it references.
        for path in reversed(changed):
            if path.read_bytes()!=new_files[path]:raise RollbackUnconfirmed("concurrent policy prevents rollback")
            updater.replace(path,originals[path],infos[path])
        if updater.OPS_CONFIG.read_bytes()!=config_bytes:
            if updater.OPS_CONFIG.read_bytes()!=new_config:
                raise RollbackUnconfirmed("concurrent gateway prevents rollback")
            updater.replace(updater.OPS_CONFIG,config_bytes,config_info)
        try:
            guard.run([guard.NGINX,"-t"]);guard.run([guard.NGINX,"-s","reload"])
        except Exception as error:
            raise RollbackUnconfirmed("restored policy reload is unconfirmed") from error
        raise
    return new_config


def verify_live_nginx_identity():
    account=pwd.getpwnam("http");gid=grp.getgrnam("http").gr_gid
    processes={}
    for path in Path("/proc").iterdir():
        if not path.name.isdigit():continue
        try:
            fields=dict(line.split(":",1) for line in (path/"status").read_text().splitlines() if ":" in line)
            if fields.get("Name","").strip()!="nginx":continue
            processes[int(path.name)]={"parent":int(fields["PPid"]),"uid":int(fields["Uid"].split()[0]),
                                       "gid":int(fields["Gid"].split()[0])}
        except (OSError,ValueError,KeyError):continue
    masters=[pid for pid,data in processes.items() if data["uid"]==0 and data["parent"]==1]
    if len(masters)!=1:raise ValueError("one host nginx master required")
    workers=[data for data in processes.values() if data["parent"]==masters[0]]
    if not workers or any(data["uid"]!=account.pw_uid or data["gid"]!=gid for data in workers):
        raise ValueError("reviewed live nginx worker identity required")


def execute(plan, apply=False):
    fields={"origin_plan","expected_guard_hashes","credential_input_path",
            "credential_input_sha256","public_bootstrap_authorized",
            "trace_report_key","trace_plan_sha256","trace_refusal_timestamp","expected_peer_sha256"}
    if os.geteuid()!=0 or not isinstance(plan,dict) or set(plan)!=fields:
        raise ValueError("root and exact captured auth plan required")
    if type(plan["public_bootstrap_authorized"]) is not bool:
        raise ValueError("explicit bootstrap decision required")
    for key in ("credential_input_sha256","trace_plan_sha256","expected_peer_sha256"):
        if not re.fullmatch(r"[a-f0-9]{64}",plan[key]):raise ValueError("captured digest required")
    if not re.fullmatch(r"[a-z0-9-]{8,64}",plan["trace_report_key"]):
        raise ValueError("fixed trace report binding required")
    if not re.fullmatch(r"[0-9]{1,12}\.[0-9]{1,6}",plan["trace_refusal_timestamp"]):
        raise ValueError("exact refusal occurrence required")
    updater.protected_directory(updater.PRIVATE,0o077)
    work=Path(tempfile.mkdtemp(prefix="ota-auth-",dir=str(updater.PRIVATE)))
    os.chmod(str(work),0o700)
    guard.create(work/"origin.plan.json",json.dumps(plan["origin_plan"]))
    origin=guard.read_plan(work/"origin.plan.json")
    response=updater.REPORT_DIRECTORY/(origin["report_key"]+".json")
    updater.protected_directory(response.parent.resolve(),0o022)
    if response.exists() or response.is_symlink():raise ValueError("auth report already exists")
    owner=pwd.getpwnam(origin["report_owner"])
    input_path=Path(plan["credential_input_path"])
    identity,input_bytes=read_identity(input_path,plan["credential_input_sha256"],owner)
    peer=auth.observed_public_peer(updater.PRIVATE,plan["trace_report_key"],
                                  plan["trace_plan_sha256"],plan["trace_refusal_timestamp"])
    if guard.digest(peer.encode())!=plan["expected_peer_sha256"]:
        raise ValueError("observed HTTPS peer differs from reviewed field connection")
    paths=[guard.HTTP_INCLUDE,Path(origin["guard_directory"])/"user.conf.smartbox_ota_origin_guard"]
    expected=plan["expected_guard_hashes"]
    if set(expected)!=set(map(str,paths)):raise ValueError("two exact guard bindings required")
    originals={p:guard.regular(p).read_bytes() for p in paths}
    if any(guard.digest(originals[p])!=expected[str(p)] for p in paths):
        raise ValueError("operating origin drift")
    config_bytes=guard.regular(updater.OPS_CONFIG).read_bytes()
    config=json.loads(config_bytes)
    if config["owner"]!=origin["report_owner"] or config["origin_guard_hashes"]!=expected:
        raise ValueError("gateway origin binding drift")
    if (paths[0].parent/"http.smartbox_ota_trace.conf").exists():
        raise ValueError("another live observer must clean first")
    for p in paths:updater.protected_directory(p.parent,0o022)
    runtime=auth.RUNTIME
    updater.protected_directory(runtime.parent.resolve(),0o022)
    if runtime.exists() and any(runtime.iterdir()):
        raise ValueError("another bootstrap session is active")
    service=guard.regular(Path(origin["sources"]["service"]["path"])).read_text()
    body=auth.proxy_body(service,origin["guard_directory"])
    new_http,new_server=auth.render(origin,body)
    new_files={paths[0]:new_http.encode(),paths[1]:new_server.encode()}
    old_http,old_server=guard.render(origin)
    legacy=originals[paths[0]]==old_http.encode() and originals[paths[1]]==old_server.encode()
    reentry=all(originals[p]==data for p,data in new_files.items())
    if not legacy and not reentry:raise ValueError("only legacy or matching authenticated profile supported")
    verify_live_nginx_identity()
    gid=grp.getgrnam("http").gr_gid
    created_directory=created_password=False
    activated=False;new_config=config_bytes;preserve_password=False
    try:
        if not auth.AUTH_DIRECTORY.exists():
            auth.AUTH_DIRECTORY.mkdir(mode=0o750);os.chown(str(auth.AUTH_DIRECTORY),0,gid)
            created_directory=True
        updater.protected_directory(auth.AUTH_DIRECTORY,0o022)
        if auth.AUTH_DIRECTORY.stat().st_gid!=gid:
            raise ValueError("hashed-password directory group mismatch")
        if legacy:
            if auth.AUTH_FILE.is_symlink():
                raise ValueError("hashed-password symlink is forbidden")
            if not auth.AUTH_FILE.exists():
                guard.create(auth.AUTH_FILE,auth.password_line(identity))
                os.chown(str(auth.AUTH_FILE),0,gid);os.chmod(str(auth.AUTH_FILE),0o640)
                created_password=True
        info=guard.regular(auth.AUTH_FILE).stat()
        if info.st_gid!=gid or stat.S_IMODE(info.st_mode)!=0o640:
            raise ValueError("private hashed-password mode/group required")
        password_bytes=auth.AUTH_FILE.read_bytes()
        if not password_bytes.startswith((identity["username"]+":$6$").encode()) or password_bytes.count(b"\n")!=1:
            raise ValueError("one bound salted password identity required")
        users=validate_full(origin,work,new_http,new_server)
        closed=work/"fixture-closed";closed.mkdir(mode=0o700)
        cases=auth.fixture(origin,closed,identity,auth.AUTH_FILE,worker_user="http")
        if plan["public_bootstrap_authorized"]:
            opened=work/"fixture-bootstrap";opened.mkdir(mode=0o700)
            cases+=auth.fixture(origin,opened,identity,auth.AUTH_FILE,worker_user="http",bootstrap_peer=peer)
        guard.read_plan_from_sources(origin)
        if updater.OPS_CONFIG.read_bytes()!=config_bytes or any(p.read_bytes()!=v for p,v in originals.items()):
            raise ValueError("concurrent policy mutation")
        if any(p.read_bytes()!=v for p,v in users.items()):
            raise ValueError("concurrent user-include mutation")
        if any(Path(origin["guard_directory"]).glob("proxy.conf*")):
            raise ValueError("proxy includes appeared during validation")
        for p,data in originals.items():guard.create(work/(p.name+".original"),data.decode())
        guard.create(work/"gateway.original.json",config_bytes.decode())
        infos={p:p.stat() for p in paths};config_info=updater.OPS_CONFIG.stat()
        if apply and legacy:
            try:new_config=activate(origin,originals,infos,config_bytes,config_info,new_files)
            except RollbackUnconfirmed:
                preserve_password=True;raise
            activated=True
        result={"state":"policy_installed" if apply else "validated",
                "captured_at_utc":datetime.now(timezone.utc).isoformat(),
                "full_candidate_config_valid":True,"private_auth_behavior_cases":cases,
                "public_bootstrap_authorized":plan["public_bootstrap_authorized"],
                "public_bootstrap_active":False,"broker_credentials_changed":False,
                "ssh_permissions_changed":False,"operating_configuration_changed":activated,
                "installed_file_hashes":{str(p):guard.digest(data) for p,data in new_files.items()},
                "observed_peer_fingerprint":guard.digest(peer.encode())[:16],
                "hashed_credential_file_sha256":guard.digest(password_bytes)}
        if apply:
            try:
                if not input_path.is_symlink() and input_path.read_bytes()==input_bytes:input_path.unlink()
            except OSError:
                pass
            def restore():
                if any(p.read_bytes()!=data for p,data in new_files.items()) or updater.OPS_CONFIG.read_bytes()!=new_config:
                    raise RuntimeError("concurrent authenticated policy prevents rollback")
                if activated:
                    for p in reversed(paths):updater.replace(p,originals[p],infos[p])
                    updater.replace(updater.OPS_CONFIG,config_bytes,config_info)
                    guard.run([guard.NGINX,"-t"]);guard.run([guard.NGINX,"-s","reload"])
                if created_password:
                    if auth.AUTH_FILE.read_bytes()!=password_bytes:raise RuntimeError("password file changed")
                    auth.AUTH_FILE.unlink()
                if created_directory and not any(auth.AUTH_DIRECTORY.iterdir()):auth.AUTH_DIRECTORY.rmdir()
            return origin,work,owner,identity,peer,response,result,restore
        return result
    finally:
        if (not apply or not (activated or reentry)) and not preserve_password:
            if created_password and auth.AUTH_FILE.exists():auth.AUTH_FILE.unlink()
            if created_directory and auth.AUTH_DIRECTORY.exists() and not any(auth.AUTH_DIRECTORY.iterdir()):
                auth.AUTH_DIRECTORY.rmdir()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan",type=Path,required=True)
    parser.add_argument("--expected-plan-sha256",required=True)
    parser.add_argument("--apply",action="store_true")
    args=parser.parse_args()
    raw=args.plan.read_bytes()
    if len(raw)>131072 or guard.digest(raw)!=args.expected_plan_sha256:
        raise ValueError("captured auth plan mismatch")
    prepared=execute(json.loads(raw),args.apply)
    if args.apply:
        try:
            from . import bootstrap_authenticated_ota as bootstrap
        except ImportError:
            import bootstrap_authenticated_ota as bootstrap
        bootstrap.start_session(*prepared)
    else:print(json.dumps(prepared,sort_keys=True))


if __name__=="__main__":
    main()
