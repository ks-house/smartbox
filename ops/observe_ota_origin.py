#!/usr/bin/env python3
"""Bounded OTA-only HTTP trace; optionally repair an observed private NAS LAN."""
import argparse
from datetime import datetime, timezone
import fcntl
import ipaddress
import json
import os
from pathlib import Path
import pwd
import signal
import socket
import struct
import tempfile
import time
try:
    from . import install_ota_origin_guard as guard
    from . import update_ota_origin_guard as updater
except ImportError:
    import install_ota_origin_guard as guard
    import update_ota_origin_guard as updater

TRACE_HTTP_NAME = "http.smartbox_ota_trace.conf"
TRACE_SERVER_NAME = "user.conf.smartbox_ota_trace"
PRIVATE_RANGES = tuple(ipaddress.ip_network(x) for x in
                       ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))


def http_config(peer="$realip_remote_addr"):
    if peer not in ("$realip_remote_addr", "$fixture_peer"):
        raise ValueError("fixed peer variable required")
    # $uri changes during Synology error_page redirects. Map the immutable
    # request to fixed path labels, never serialize its raw query string.
    return ('map $request_uri $smartbox_ota_trace_path {\n'
            ' default "";\n'
            ' ~^/firmware[.]bin(?:[?]|$) /firmware.bin;\n'
            ' ~^/version[.]json(?:[?]|$) /version.json;\n}\n'
            'map $smartbox_ota_trace_path $smartbox_ota_trace_enabled {\n'
            ' default 1;\n "" 0;\n}\n'
            'log_format smartbox_ota_trace escape=json '
            '\'{"at":"$msec","peer":"' + peer +
            '","uri":"$smartbox_ota_trace_path","method":"$request_method",'
            '"status":$status,"agent":"$http_user_agent"}\';\n')


def server_config(logfile):
    if any(c in str(logfile) for c in "\"';{}\n\r "):
        raise ValueError("unsafe private log path")
    return ("access_log " + str(logfile) +
            " smartbox_ota_trace if=$smartbox_ota_trace_enabled;\n")


def interface_networks():
    result = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for _, name in socket.if_nameindex():
            if not name.startswith(("eth", "bond", "ovs")):
                continue
            try:
                request = struct.pack("256s", name.encode()[:15])
                address = socket.inet_ntoa(fcntl.ioctl(sock.fileno(), 0x8915, request)[20:24])
                mask = socket.inet_ntoa(fcntl.ioctl(sock.fileno(), 0x891b, request)[20:24])
                network = ipaddress.ip_network(address + "/" + mask, strict=False)
                if network.prefixlen >= 24 and any(network.subnet_of(x) for x in PRIVATE_RANGES):
                    result.append(str(network))
            except OSError:
                continue
    return sorted(set(result))


def repair_candidate(records, networks, allowed):
    eligible = set()
    parsed = [ipaddress.ip_network(x) for x in networks]
    accepted = [ipaddress.ip_network(x) for x in allowed]
    for record in records:
        if record.get("uri") != "/firmware.bin" or record.get("status") != 403:
            continue
        if not str(record.get("agent", "")).lower().startswith("esp32"):
            continue
        try:
            peer = ipaddress.ip_address(record["peer"])
        except (ValueError, KeyError):
            continue
        if peer.version != 4 or not any(peer in x for x in PRIVATE_RANGES):
            continue
        if any(peer.version == x.version and peer in x for x in accepted):
            continue
        matches = [x for x in parsed if x.version == 4 and x.prefixlen >= 24 and peer in x
                   and any(x.subnet_of(p) for p in PRIVATE_RANGES)]
        if len(matches) == 1:
            eligible.add(str(matches[0]))
    if len(eligible) > 1:
        raise ValueError("multiple observed private LANs; explicit review required")
    return next(iter(eligible), None)


def read_records(path):
    with path.open("rb") as stream:
        stream.seek(0, os.SEEK_END)
        size = stream.tell()
        stream.seek(max(0, size - 1048576))
        raw = stream.read(1048576)
    result = []
    for line in raw.splitlines()[-200:]:
        if len(line) > 16384:
            continue
        try:
            row = json.loads(line)
        except (ValueError, UnicodeDecodeError):
            continue
        if isinstance(row, dict) and row.get("uri") in ("/version.json", "/firmware.bin"):
            result.append(row)
    return result


def summary(records, networks):
    result = []
    for row in records[-50:]:
        try:
            peer = ipaddress.ip_address(row["peer"])
        except (ValueError, KeyError):
            continue
        matching = [n for n in networks if peer.version == 4 and peer in ipaddress.ip_network(n)]
        result.append({"at": row.get("at"), "uri": row.get("uri"),
                       "status": row.get("status"), "method":
                       row.get("method") if row.get("method") in ("HEAD", "GET") else "OTHER", "esp32_agent":
                       str(row.get("agent", "")).lower().startswith("esp32"),
                       "peer_scope": "LOOPBACK" if peer.is_loopback else
                       "TAILNET" if peer.version == 4 and peer in ipaddress.ip_network("100.64.0.0/10") else
                       matching[0] if len(matching) == 1 else
                       ("PRIVATE_UNMATCHED" if any(peer.version == n.version and peer in n for n in PRIVATE_RANGES) else "PUBLIC")})
    return result


def capture_self_check(origin, logfile, timeout=15):
    """Do not announce active until real loopback HEADs reach the new log."""
    deadline = time.monotonic() + timeout
    required = {"/version.json", "/firmware.bin"}
    while time.monotonic() < deadline:
        for uri in sorted(required):
            guard.head_local(origin, uri)
        observed = set()
        for row in read_records(logfile):
            try:
                loopback = ipaddress.ip_address(row.get("peer", "")).is_loopback
            except ValueError:
                loopback = False
            if loopback and row.get("method") == "HEAD" and row.get("status") == 200:
                observed.add(row["uri"])
        if required.issubset(observed):
            return {"verified_paths": sorted(required), "method": "HEAD",
                    "actual_loopback_log_verified": True}
        time.sleep(0.5)
    raise RuntimeError("new trace did not record both successful local HEAD paths")


def report(path, owner, data):
    fd, temporary = tempfile.mkstemp(prefix=".trace-report-", dir=str(path.parent))
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w") as stream:
            json.dump({**data, "captured_at_utc": datetime.now(timezone.utc).isoformat()}, stream, sort_keys=True)
            stream.flush();os.fsync(stream.fileno())
        os.chown(temporary, owner.pw_uid, owner.pw_gid)
        os.replace(temporary, str(path))
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def remove_exact(files):
    changed = False
    for path, content in reversed(files):
        if not path.exists() and not path.is_symlink():
            continue
        if path.is_symlink() or path.stat().st_uid != 0 or path.read_bytes() != content.encode():
            raise RuntimeError("trace configuration changed; refusing unsafe cleanup")
        try:
            path.unlink();changed = True
        except FileNotFoundError:
            pass
    if changed:
        guard.run([guard.NGINX, "-t"]);guard.run([guard.NGINX, "-s", "reload"])


def validate_full(origin, work, http, server):
    sources = {k: Path(v["path"]).read_text() for k, v in origin["sources"].items()}
    service = Path(origin["sources"]["service"]["path"])
    portal = Path(origin["sources"]["portal"]["path"])
    guard.create(work/"trace-http.conf", http)
    guard.create(work/"trace-server.conf", server)
    guard.create(work/"service.conf", sources["service"]+"\ninclude "+str(work/"trace-server.conf")+";\n")
    marker = "include conf.d/"+service.name+"*;"
    if sources["portal"].count(marker) != 1:
        raise ValueError("pinned portal include layout changed")
    guard.create(work/"portal.conf", sources["portal"].replace(marker, "include "+str(work/"service.conf")+";"))
    sites = "include sites-enabled/*;"
    includes = "include conf.d/http.*.conf;"
    if sources["global"].count(sites) != 1 or sources["global"].count(includes) != 1:
        raise ValueError("pinned global include layout changed")
    replacements = []
    for path in sorted(Path("/etc/nginx/sites-enabled").iterdir()):
        replacements.append("include "+str(work/"portal.conf" if path.resolve() == portal.resolve() else path)+";")
    candidate = sources["global"].replace(sites, "\n".join(replacements)).replace(
        includes, includes+"\ninclude "+str(work/"trace-http.conf")+";")
    fd, name = tempfile.mkstemp(prefix=".ota-trace-validation-", dir="/etc/nginx")
    try:
        with os.fdopen(fd, "w") as stream:stream.write(candidate)
        guard.run([guard.NGINX, "-t", "-p", "/etc/nginx/", "-c", name])
    finally:
        Path(name).unlink()


def start_trace(update, seconds=300, repair_private=False):
    if os.geteuid() != 0 or not 150 <= seconds <= 300:
        raise ValueError("root and a bounded150..300 second trace required")
    if set(update) != {"origin_plan", "expected_guard_hashes"}:
        raise ValueError("unexpected trace plan")
    updater.protected_directory(updater.PRIVATE, 0o077)
    lock = os.open(str(updater.PRIVATE/"ota-origin-trace.lock"),
                   os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX|fcntl.LOCK_NB)
    except OSError:
        os.close(lock);raise ValueError("another bounded trace session is active")
    work = Path(tempfile.mkdtemp(prefix="ota-trace-", dir=str(updater.PRIVATE)))
    os.chmod(str(work), 0o700)
    guard.create(work/"plan.json", json.dumps(update["origin_plan"]))
    origin = guard.read_plan(work/"plan.json")
    configuration = json.loads(guard.regular(updater.OPS_CONFIG).read_text())
    expected = update["expected_guard_hashes"]
    paths = [guard.HTTP_INCLUDE, Path(origin["guard_directory"])/"user.conf.smartbox_ota_origin_guard"]
    if set(expected) != set(map(str, paths)) or configuration["origin_guard_hashes"] != expected:
        raise ValueError("origin binding changed")
    geo, rule = guard.render(origin)
    if any(guard.digest(guard.regular(p).read_bytes()) != expected[str(p)] for p in paths):
        raise ValueError("installed origin changed")
    if paths[0].read_text() != geo or paths[1].read_text() != rule:
        raise ValueError("origin plan does not match installed guard")
    if configuration["owner"] != origin["report_owner"] or len(origin["report_key"]) > 57:
        raise ValueError("operator or report binding mismatch")
    owner = pwd.getpwnam(origin["report_owner"])
    response = Path("/var/run/sgk-broker-audit")/(origin["report_key"]+".json")
    if response.exists() or response.is_symlink():
        raise ValueError("trace report exists; refusing repeat")
    updater.protected_directory(response.parent.resolve(), 0o022)
    runtime_base = Path("/var/run").resolve()
    updater.protected_directory(runtime_base, 0o022)
    runtime = runtime_base/("sgk-"+origin["report_key"])
    runtime.mkdir(mode=0o700)
    logfile = work/"requests.jsonl";guard.create(logfile, "")
    http = http_config();server = server_config(logfile)
    validate_full(origin, work, http, server)
    loaders = [(guard.HTTP_INCLUDE.parent/TRACE_HTTP_NAME,
                "include "+str(runtime/"http.*.conf")+";\n"),
               (Path(origin["guard_directory"])/TRACE_SERVER_NAME,
                "include "+str(runtime/"server.*.conf")+";\n")]
    for parent in {p.parent for p, _ in loaders}:updater.protected_directory(parent, 0o022)
    if any(p.exists() or p.is_symlink() for p, _ in loaders):
        raise ValueError("another trace loader exists")
    files = [(runtime/"http.trace.conf", http), (runtime/"server.trace.conf", server), *loaders]
    networks = interface_networks()
    initial = {"state":"waiting_for_agent", "wait_seconds":600, "seconds":seconds,
               "repair_private_enabled":repair_private, "nas_lans":networks,
               "requests":[], "capture_ready":False, "trace_cleaned":True, "credentials_changed":False,
               "broker_changed":False}
    report(response, owner, initial)
    pid = os.fork()
    if pid == 0:
        os.setsid()
        fd = os.open(str(work/"worker.log"), os.O_WRONLY|os.O_CREAT|os.O_APPEND, 0o600)
        os.dup2(fd, 1);os.dup2(fd, 2);os.close(fd)
        null = os.open("/dev/null", os.O_RDONLY);os.dup2(null, 0);os.close(null)
        state = {**initial, "pid":os.getpid()}
        watchdog = None;installed = False
        try:
            watchdog = os.fork()
            if watchdog == 0:
                try:
                    time.sleep(600+seconds+90)
                    remove_exact(files)
                finally:
                    os._exit(0)
            state["watchdog_pid"] = watchdog
            report(response, owner, state)
            stop = [False]
            signal.signal(signal.SIGTERM, lambda *_:stop.__setitem__(0, True))
            signal.signal(signal.SIGINT, lambda *_:stop.__setitem__(0, True))
            waiting = time.monotonic()+600
            activated = False
            while time.monotonic() < waiting and not stop[0]:
                with response.open("rb") as stream:raw = stream.read(8193)
                if len(raw) <= 8192:
                    try:command = json.loads(raw)
                    except ValueError:command = {}
                    if isinstance(command,dict) and command.get("activate") is True:
                        activated = True;break
                time.sleep(0.5)
            if not activated:
                state["state"] = "expired"
            else:
                guard.read_plan_from_sources(origin)
                current = json.loads(updater.OPS_CONFIG.read_text())
                if current["origin_guard_hashes"] != expected or any(
                        guard.digest(p.read_bytes()) != expected[str(p)] for p in paths):
                    raise ValueError("origin changed while awaiting activation")
                if any(p.exists() or p.is_symlink() for p, _ in files):
                    raise ValueError("trace files appeared while awaiting activation")
                written = [];installed = True;state["trace_cleaned"] = False
                try:
                    for path, content in files:
                        guard.create(path,content);written.append((path,content))
                    guard.run([guard.NGINX,"-t"]);guard.run([guard.NGINX,"-s","reload"])
                    state["state"] = "verifying_capture"
                    report(response,owner,state)
                    state["capture_check"] = capture_self_check(origin,logfile)
                    state["capture_ready"] = True
                    installed = True
                except Exception:
                    remove_exact(written);installed = False;state["trace_cleaned"] = True;raise
                state.update(state="active",trace_cleaned=False)
                deadline = time.monotonic()+seconds;repaired = False
                while time.monotonic() < deadline and not stop[0]:
                    records = read_records(logfile)
                    state["requests"] = summary(records,networks)
                    candidate = (repair_candidate(records,networks,origin["allowed_cidrs"])
                                 if repair_private and not repaired else None)
                    if candidate and time.monotonic() < deadline-120:
                        repair_origin = {**origin,"report_key":origin["report_key"]+"-repair"}
                        updater.execute({"origin_plan":repair_origin,
                                         "expected_guard_hashes":expected,
                                         "additional_lan_cidr":candidate},True)
                        repaired = True
                        state.update(state="repaired",repaired_private_lan=candidate)
                    report(response,owner,state)
                    time.sleep(1)
                state["state"] = "finished"
        except Exception as error:
            failed_stage = state["state"]
            state.update(state="failed",failed_stage=failed_stage,error_type=type(error).__name__)
            try:
                state["requests"] = summary(read_records(logfile),networks)
                state["capture_log_bytes"] = logfile.stat().st_size
            except OSError:
                state["capture_log_readable"] = False
            guard.create(work/"error.json",json.dumps({"error_type":type(error).__name__,
                         "message":str(error),"failed_query":guard.FAILED_QUERY}))
        finally:
            if installed:
                try:
                    remove_exact(files);state["trace_cleaned"] = True
                except Exception as error:
                    state.update(trace_cleaned=False,cleanup_error=type(error).__name__)
            report(response,owner,state)
            if watchdog and state["trace_cleaned"]:
                try:os.kill(watchdog,signal.SIGTERM)
                except ProcessLookupError:pass
            os._exit(0)
    print(json.dumps({"ota_trace_session_ready":True,"pid":pid,"wait_seconds":600,
                      "trace_seconds":seconds,"repair_private_enabled":repair_private,
                      "general_shell":False}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--expected-plan-sha256", required=True)
    parser.add_argument("--seconds", type=int, default=300)
    parser.add_argument("--repair-private-nas-lan", action="store_true")
    args = parser.parse_args()
    with args.plan.open("rb") as stream:raw = stream.read(131073)
    if len(raw)>131072 or guard.digest(raw) != args.expected_plan_sha256:
        raise ValueError("pinned trace plan mismatch")
    start_trace(json.loads(raw), args.seconds, args.repair_private_nas_lan)


if __name__ == "__main__":
    main()
