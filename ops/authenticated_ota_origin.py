#!/usr/bin/env python3
"""Authenticated OTA policy and isolated native-nginx verification."""
import base64
import grp
import hashlib
import http.client
import ipaddress
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import threading
import socketserver
import tempfile
import time
from http.server import BaseHTTPRequestHandler
try:
    from . import install_ota_origin_guard as guard
except ImportError:
    import install_ota_origin_guard as guard

AUTH_DIRECTORY = Path("/usr/local/etc/smartbox-ota")
AUTH_FILE = AUTH_DIRECTORY/"htpasswd"
RUNTIME = Path("/var/run").resolve()/"smartbox-ota-bootstrap"


def credentials(raw):
    value = json.loads(raw)
    if not isinstance(value, dict) or set(value) != {"username", "password"}:
        raise ValueError("exact OTA credentials required")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", value["username"]):
        raise ValueError("invalid OTA principal")
    if not re.fullmatch(r"[a-f0-9]{64}", value["password"]):
        raise ValueError("random 256-bit OTA password required")
    return value


def password_line(identity, openssl="/usr/bin/openssl"):
    result = subprocess.run([openssl, "passwd", "-6", "-stdin"],
                            input=(identity["password"]+"\n").encode(),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
    digest = result.stdout.decode().strip()
    if result.returncode or not digest.startswith("$6$") or "\n" in digest:
        raise RuntimeError("private password hashing failed")
    return identity["username"]+":"+digest+"\n"


def proxy_body(service, directory):
    matches = re.findall(r"(?ms)^\s*location ~ / \{\n(.*?)^\s*\}", service)
    if len(matches) != 1:
        raise ValueError("pinned single proxy location required")
    body = matches[0]
    if any(x in body for x in ("{", "}", "auth_basic", "Authorization", "access_log")):
        raise ValueError("unexpected proxy policy requires review")
    if len(re.findall(r"(?m)^\s*proxy_pass\s+http://[^;]+;\s*$", body)) != 1:
        raise ValueError("one preserved HTTP upstream required")
    if any(Path(directory).glob("proxy.conf*")):
        raise ValueError("additional proxy includes require explicit review")
    return body


def render(origin, body, auth_file=AUTH_FILE, runtime=RUNTIME,
           peer="$realip_remote_addr"):
    if peer not in ("$realip_remote_addr", "$fixture_peer"):
        raise ValueError("fixed original peer required")
    for path in (auth_file, runtime):
        if not str(path).startswith("/") or re.search(r"[\s;{}\"\']", str(path)):
            raise ValueError("safe absolute policy paths required")
    http = guard.render(origin, address=peer)[0]
    http += "map "+peer+" $smartbox_ota_bootstrap_allowed {\n default 0;\n"
    http += " include "+str(runtime/"peer.*.conf")+";\n}\n"
    http += "map $msec $smartbox_ota_bootstrap_time_valid {\n default 0;\n include "+str(runtime/"time.*.conf")+";\n}\n"
    http += ('map "$smartbox_ota_origin_allowed:$smartbox_ota_bootstrap_allowed:$smartbox_ota_bootstrap_time_valid" '
             '$smartbox_ota_auth_realm {\n default "SmartBox OTA";\n'
             ' ~^1: off;\n "0:1:1" off;\n}\n')
    http += "include "+str(runtime/"http.observe.*.conf")+";\n"
    server = ""
    for uri in ("/version.json", "/firmware.bin"):
        server += "location = "+uri+" {\n"+body
        server += (' limit_except GET { deny all; }\n keepalive_timeout 0;\n auth_basic $smartbox_ota_auth_realm;\n'
                   ' auth_basic_user_file '+str(auth_file)+';\n'
                   ' proxy_set_header Authorization "";\n'
                   ' include '+str(runtime/"location.observe.*.conf")+';\n}\n')
    return http, server


def observed_public_peer(private, trace_key, expected_plan_sha256, timestamp):
    matches = []
    for work in sorted(private.glob("ota-trace-*"))[:256]:
        plan_path = work/"plan.json"
        if not plan_path.is_file():
            continue
        plan = json.loads(guard.regular(plan_path).read_text())
        if plan.get("report_key") != trace_key:
            continue
        if guard.digest(json.dumps(plan, sort_keys=True).encode()) != expected_plan_sha256:
            raise ValueError("captured trace plan drift")
        with guard.regular(work/"requests.jsonl").open("rb") as stream:
            stream.seek(0,os.SEEK_END);size=stream.tell()
            stream.seek(max(0,size-1048576));raw=stream.read(1048576)
        rows = raw.splitlines()
        for line in rows[-200:]:
            if len(line)>16384:continue
            try:row = json.loads(line)
            except (ValueError,UnicodeDecodeError):continue
            if not isinstance(row,dict):continue
            if (row.get("at") != timestamp or row.get("uri") != "/firmware.bin"
                    or row.get("method") != "GET" or row.get("status") != 403
                    or not str(row.get("agent", "")).lower().startswith("esp32")):
                continue
            peer = ipaddress.ip_address(row["peer"])
            if peer.version != 4 or not peer.is_global or peer.is_multicast:
                raise ValueError("measured single global IPv4 peer required")
            matches.append(str(peer))
    if len(matches) != 1:
        raise ValueError("one exact captured OTA refusal required")
    return matches[0]


def bootstrap_entry(peer):
    address = ipaddress.ip_address(peer)
    if address.version != 4 or not address.is_global or address.is_multicast:
        raise ValueError("one global IPv4 /32 only")
    return str(address)+" 1;\n"


def bootstrap_time_entry(start_epoch, seconds=300):
    if type(start_epoch) is not int or not 1 <= start_epoch < 10000000000:
        raise ValueError("fixed server epoch required")
    if type(seconds) is not int or not 1 <= seconds <= 300:
        raise ValueError("bootstrap requests expire within300 seconds")
    values="|".join(str(n) for n in range(start_epoch,start_epoch+seconds))
    return '"~^(?:'+values+')[.][0-9]+$" 1;\n'


def fixture(origin, work, identity, auth_file, worker_user=None, bootstrap_peer=None,
            bootstrap_expired=False):
    """Real auth/access phases via a loopback dummy upstream; no public listener."""
    forwarded = []
    class Upstream(BaseHTTPRequestHandler):
        def handle_request(self):
            forwarded.append("Authorization" in self.headers)
            payload = b"private-ota-fixture"
            self.send_response(200);self.send_header("Content-Length",str(len(payload)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(payload)
        do_GET = do_HEAD = handle_request
        def log_message(self, *_):pass
    class UnixHTTPServer(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
        daemon_threads = True
    upstream_directory = Path(tempfile.mkdtemp(prefix="ota-auth-upstream-",
                                              dir=str(RUNTIME.parent if worker_user else work)))
    if worker_user:
        gid = grp.getgrnam(worker_user).gr_gid
        os.chown(str(upstream_directory),0,gid);os.chmod(str(upstream_directory),0o710)
    upstream_socket = upstream_directory/"http.sock"
    upstream = UnixHTTPServer(str(upstream_socket),Upstream)
    os.chmod(str(upstream_socket),0o660)
    if worker_user:os.chown(str(upstream_socket),0,gid)
    thread = threading.Thread(target=upstream.serve_forever,daemon=True);thread.start()
    runtime = work/"runtime"
    if bootstrap_peer:
        runtime.mkdir(mode=0o700)
        guard.create(runtime/"peer.test.conf",bootstrap_entry(bootstrap_peer))
        guard.create(runtime/"time.test.conf",bootstrap_time_entry(int(time.time())-(600 if bootstrap_expired else 0)))
    body = " proxy_pass http://smartbox_auth_fixture;\n"
    http_config, server_config = render(origin,body,auth_file,runtime,peer="$fixture_peer")
    config = work/"auth-fixture.conf";sock = work/"auth-fixture.sock"
    temp = "".join(key+" "+str(work/name)+";\n" for key,name in [
        ("client_body_temp_path","client-temp"),("proxy_temp_path","proxy-temp"),
        ("fastcgi_temp_path","fastcgi-temp"),("uwsgi_temp_path","uwsgi-temp"),
        ("scgi_temp_path","scgi-temp")])
    user = "user "+worker_user+";\n" if worker_user else ""
    text = user+"pid "+str(work/"nginx.pid")+";\nerror_log "+str(work/"nginx.error.log")+";\n"
    text += "events {} http { access_log off;\n"+temp
    text += 'map $request_uri $fixture_peer { default ""; ~[?&]client=(?<fixture_ip>[0-9a-fA-F:.]+) $fixture_ip; }\n'
    text += "upstream smartbox_auth_fixture { server unix:"+str(upstream_socket)+"; }\n"
    text += http_config+"server { listen unix:"+str(sock)+";\n"+server_config+"location / {\n"+body+"}\n}}\n"
    guard.create(config,text)
    output=(work/"nginx.process.log").open("wb")
    process = subprocess.Popen([guard.NGINX,"-p",str(work)+"/","-c",str(config),
                                "-g","daemon off;"],stdout=output,stderr=output)
    cases = []
    try:
        until=time.monotonic()+5
        while not sock.exists() and time.monotonic()<until:
            if process.poll() is not None:
                details="".join(p.read_text(errors="replace")[-2000:] for p in
                                (work/"nginx.process.log",work/"nginx.error.log") if p.exists())
                raise RuntimeError("isolated auth fixture did not start: "+details)
            time.sleep(0.05)
        if not sock.exists():raise RuntimeError("isolated auth fixture startup deadline")
        public=["198.51.100.10","2001:db8::10"]
        correct=base64.b64encode((identity["username"]+":"+identity["password"]).encode()).decode()
        wrong=base64.b64encode((identity["username"]+":invalid-password").encode()).decode()
        for uri in ("/version.json","/firmware.bin","/%66irmware.bin","//firmware.bin","/x/../firmware.bin"):
            for peer in public:
                cases.extend([(uri,peer,None,401),(uri,peer,wrong,401),(uri,peer,correct,200)])
        for cidr in origin["allowed_cidrs"]:
            peer=str(ipaddress.ip_network(cidr).network_address)
            cases.extend([(uri,peer,None,200) for uri in ("/version.json","/firmware.bin")])
        cases.extend([(uri,"198.51.100.10",None,200) for uri in
                      ("/gatekeeper_apk/app.apk","/firmware/gatekeeper.sgkenc")])
        if bootstrap_peer:
            cases.extend([(uri,bootstrap_peer,None,401 if bootstrap_expired else 200) for uri in ("/version.json","/firmware.bin")])
        for uri,peer,authorization,expected in cases:
            for method in ("HEAD","GET"):
                client=http.client.HTTPConnection("fixture",timeout=3)
                client.sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
                client.sock.settimeout(3);client.sock.connect(str(sock))
                headers={"X-Forwarded-For":"127.0.0.1","X-Real-IP":"127.0.0.1"}
                if authorization:headers["Authorization"]="Basic "+authorization
                client.request(method,uri+"?client="+peer,headers=headers)
                response=client.getresponse();status=response.status;response.read(128);client.close()
                if status!=expected:raise RuntimeError("isolated authenticated OTA policy mismatch")
        mutations=0
        for uri in ("/version.json","/firmware.bin"):
            for peer in ("127.0.0.1","198.51.100.10"):
                for method in ("POST","PUT","DELETE"):
                    client=http.client.HTTPConnection("fixture",timeout=3)
                    client.sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
                    client.sock.settimeout(3);client.sock.connect(str(sock))
                    client.request(method,uri+"?client="+peer,headers={"Authorization":"Basic "+correct})
                    response=client.getresponse();status=response.status;response.read(1024);client.close()
                    if status!=403:raise RuntimeError("read-only OTA identity accepted a mutating method")
                    mutations+=1
        if any(forwarded):raise RuntimeError("OTA authorization reached the upstream")
        return len(cases)*2+mutations
    finally:
        process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
        output.close();upstream.shutdown();upstream.server_close();thread.join(timeout=2)
        upstream_socket.unlink();upstream_directory.rmdir()
