"""Bounded, blocking-only SmartBox manifest failure fixture.

The public renderer binds original peer, OTA username, URI and server time.
It rewrites only the manifest to a verified missing upstream path, after which
the existing auth/access/proxy phases still run. It never permits a download.
"""
import base64
import hashlib
import http.client
import ipaddress
import json
import os
from pathlib import Path
import re
import socket
import socketserver
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler


def validated(peer, username, started, seconds):
    address = ipaddress.ip_address(peer)
    if address.version != 4 or address.is_multicast or address.is_unspecified:
        raise ValueError("one exact IPv4 peer required")
    if not isinstance(username, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", username):
        raise ValueError("one exact OTA username required")
    if type(started) is not int or started < 1 or type(seconds) is not int or not 1 <= seconds <= 120:
        raise ValueError("server-clock window must be at most120 seconds")
    return str(address)


def missing_path(peer, username, started, seconds):
    peer = validated(peer, username, started, seconds)
    fingerprint = hashlib.sha256(
        (peer + ":" + username + ":" + str(started) + ":" + str(seconds)).encode()
    ).hexdigest()[:16]
    return "/__smartbox_ota_diagnostic_" + fingerprint


def _render(peer, username, started, seconds, peer_variable, clock_variable):
    peer = validated(peer, username, started, seconds)
    if peer_variable not in {"$realip_remote_addr", "$arg_peer"} or clock_variable not in {"$msec", "$fixture_clock"}:
        raise ValueError("closed fixture variables required")
    stamps = "|".join(str(value) for value in range(started, started + seconds))
    http = (
        "geo " + peer_variable + " $smartbox_diag_peer_allowed {\n"
        " default 0;\n " + peer + "/32 1;\n}\n"
        "map " + clock_variable + " $smartbox_diag_time_open {\n"
        ' default 0;\n "~^(?:' + stamps + ')[.][0-9]{3}$" 1;\n}\n'
        'map "$uri:$smartbox_diag_peer_allowed:$remote_user:$smartbox_diag_time_open" $smartbox_diag_manifest_fault {\n'
        ' default 0;\n "/version.json:1:' + username + ':1" 1;\n}\n'
    )
    location = ("if ($smartbox_diag_manifest_fault) { rewrite ^ " +
                missing_path(peer, username, started, seconds) + " break; }\n")
    return http, location


def render_fault(peer, username, started, seconds):
    return _render(peer, username, started, seconds, "$realip_remote_addr", "$msec")


def diagnostic_snapshot(value):
    if not isinstance(value, dict) or value.get("fsm_state") != "IDLE":
        raise ValueError("fresh idle telemetry required")
    ota = value.get("ota")
    if not isinstance(ota, dict) or set(ota) != {
        "state", "phase", "attempts", "manifest_http_code", "updater_error", "request_pending"
    }:
        raise ValueError("deployed bounded diagnostic fields required")
    for key in ("attempts", "manifest_http_code", "updater_error"):
        if type(ota[key]) is not int:
            raise ValueError("numeric diagnostic required")
    if type(value.get("timestamp")) is not int or value["timestamp"] < 0 or type(ota["request_pending"]) is not bool:
        raise ValueError("monotonic timestamp and queue bit required")
    if ota["attempts"] < 0 or ota["request_pending"] or ota["phase"] not in {"IDLE", "VERSION", "FIRMWARE", "REBOOT_PENDING"}:
        raise ValueError("completed diagnostic required")
    return {"timestamp": value["timestamp"], "fsm_state": "IDLE", "ota": dict(ota)}


def acceptance(before, failed, recovered):
    values = [diagnostic_snapshot(value) for value in (before, failed, recovered)]
    old, failure, recovery = [value["ota"] for value in values]
    if not values[0]["timestamp"] < values[1]["timestamp"] < values[2]["timestamp"]:
        raise ValueError("ordered no-reboot monotonic observations required")
    if failure["attempts"] != old["attempts"] + 1 or recovery["attempts"] != failure["attempts"] + 1:
        raise ValueError("exactly one failure and one recovery attempt required")
    if failure["state"] != "FAILED" or failure["phase"] != "VERSION" or failure["manifest_http_code"] != 404 or failure["updater_error"] != 0:
        raise ValueError("manifest failure diagnostic missing")
    if recovery["state"] != "UP_TO_DATE" or recovery["phase"] != "VERSION" or recovery["manifest_http_code"] != 200 or recovery["updater_error"] != 0:
        raise ValueError("successful no-update recovery diagnostic missing")
    return {"manifest_failure_observed": True, "no_update_recovery_observed": True,
            "no_reboot_in_observation": True, "actuator_command_sent": False,
            "firmware_transfer_expected": False}


def fixture(work, nginx="/usr/bin/nginx", openssl="/usr/bin/openssl"):
    """Real nginx phases, private Unix sockets, dummy identities only."""
    peer, username, password = "127.0.0.1", "ota_fixture", "fixture-only-password"
    started, seconds = int(time.time()), 60
    http_rules, fault = _render(peer, username, started, seconds, "$arg_peer", "$fixture_clock")
    changed_path = missing_path(peer, username, started, seconds)
    forwarded_authorization = []
    class Upstream(BaseHTTPRequestHandler):
        def serve(self):
            forwarded_authorization.append("Authorization" in self.headers)
            missing = self.path.split("?")[0] == changed_path
            payload = b"missing-diagnostic-path" if missing else b'{"version":"fixture-version"}'
            self.send_response(404 if missing else 200)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(payload)
        do_GET = do_HEAD = serve
        def log_message(self, *_):
            pass
    class UnixServer(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
        daemon_threads = True
    upstream_socket = work / "upstream.sock"
    upstream = UnixServer(str(upstream_socket), Upstream)
    thread = threading.Thread(target=upstream.serve_forever, daemon=True)
    thread.start()
    digest = subprocess.run([openssl, "passwd", "-6", "-stdin"],
                            input=(password + "\n").encode(), capture_output=True,
                            timeout=5, check=True).stdout.decode().strip()
    auth = work / "users"
    auth.write_text(username + ":" + digest + "\nother_fixture:" + digest + "\n")
    auth.chmod(0o600)
    socket_path = work / "nginx.sock"
    body = (" auth_basic \"fixture\"; auth_basic_user_file " + str(auth) + ";\n"
            " limit_except GET { deny all; }\n"
            " proxy_set_header Authorization \"\"; proxy_pass http://fixture_upstream;\n")
    locations = "".join("location = " + uri + " {\n" + fault + body + "}\n"
                        for uri in ("/version.json", "/firmware.bin"))
    temporary = "".join(kind + " " + str(work / name) + ";\n" for kind, name in [
        ("client_body_temp_path", "client-temp"), ("proxy_temp_path", "proxy-temp"),
        ("fastcgi_temp_path", "fastcgi-temp"), ("uwsgi_temp_path", "uwsgi-temp"),
        ("scgi_temp_path", "scgi-temp")])
    config = work / "nginx.conf"
    config.write_text(("user root;\n" if os.geteuid() == 0 else "") +
        "pid " + str(work / "nginx.pid") + ";\nerror_log " + str(work / "nginx.error") + ";\n"
        "events {} http { access_log off;\n" + temporary +
        'map $arg_clock $fixture_clock { default $msec; "~^[0-9]+[.][0-9]{3}$" $arg_clock; }\n' +
        "upstream fixture_upstream { server unix:" + str(upstream_socket) + "; }\n" +
        http_rules + "server { listen unix:" + str(socket_path) + ";\n" + locations +
        "location / { proxy_pass http://fixture_upstream; }\n}}\n")
    log = (work / "nginx.process.private").open("wb")
    process = subprocess.Popen([nginx, "-p", str(work) + "/", "-c", str(config), "-g", "daemon off;"],
                               stdout=log, stderr=log)
    count = 0
    try:
        deadline = time.monotonic() + 5
        while not socket_path.exists():
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("isolated diagnostic fixture startup failed; private logs retained")
            time.sleep(0.05)
        for clock, active in ((str(started) + ".123", True), (str(started + seconds) + ".000", False)):
            cases = [
                ("/version.json", peer, username, password, 404 if active else 200),
                ("/version.json", peer, username, "invalid", 401),
                ("/version.json", peer, None, None, 401),
                ("/version.json", peer, "other_fixture", password, 200),
                ("/version.json", "127.0.0.2", username, password, 200),
                ("/firmware.bin", peer, username, password, 200),
                ("/firmware.bin", peer, username, "invalid", 401),
                ("/%76ersion.json", peer, username, password, 404 if active else 200),
                ("/gatekeeper_apk/app.apk", peer, None, None, 200),
                ("/firmware/gatekeeper.sgkenc", peer, None, None, 200),
            ]
            for uri, source_peer, user, secret, expected in cases:
                for method in ("HEAD", "GET"):
                    client = http.client.HTTPConnection("fixture", timeout=3)
                    client.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    client.sock.settimeout(3)
                    client.sock.connect(str(socket_path))
                    headers = {"X-Forwarded-For": peer, "X-Real-IP": peer}
                    if user:
                        token = base64.b64encode((user + ":" + secret).encode()).decode()
                        headers["Authorization"] = "Basic " + token
                    client.request(method, uri + "?peer=" + source_peer + "&clock=" + clock, headers=headers)
                    response = client.getresponse()
                    status = response.status
                    response.read(1024)
                    client.close()
                    if status != expected:
                        raise RuntimeError("diagnostic behavior mismatch: " + method + " " + uri + " active=" + str(active) + " expected=" + str(expected) + " actual=" + str(status))
                    count += 1
            for uri in ("/version.json", "/firmware.bin"):
                for method in ("POST", "PUT", "DELETE"):
                    client = http.client.HTTPConnection("fixture", timeout=3)
                    client.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    client.sock.settimeout(3)
                    client.sock.connect(str(socket_path))
                    token = base64.b64encode((username + ":" + password).encode()).decode()
                    client.request(method, uri + "?peer=" + peer + "&clock=" + clock,
                                   headers={"Authorization": "Basic " + token})
                    response = client.getresponse()
                    status = response.status
                    response.read(1024)
                    client.close()
                    if status != 403:
                        raise RuntimeError("mutating OTA method unexpectedly accepted")
                    count += 1
        if any(forwarded_authorization):
            raise RuntimeError("OTA authorization reached dummy upstream")
        return count
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        log.close()
        upstream.shutdown()
        upstream.server_close()
        thread.join(timeout=2)
