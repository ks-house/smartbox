#!/usr/bin/env python3
"""Validate/install a path-scoped Synology OTA origin guard.

No passwords are accepted. Default mode validates only. --apply creates two new
root-controlled include files; any failed post-check removes exactly those files
and reloads the preserved configuration. Generated Web Station files stay intact.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import http.client
import ipaddress
import json
import os
from pathlib import Path
import pwd
import re
import socket
import ssl
import stat
import subprocess
import tempfile
import time

NGINX = "/usr/bin/nginx"
HTTP_INCLUDE = Path("/usr/local/etc/nginx/conf.d/http.smartbox_ota_origin_guard.conf")
FAILED_QUERY = None


def digest(data):
    return hashlib.sha256(data).hexdigest()


def regular(path):
    resolved = path.resolve(strict=True)
    info = resolved.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
        raise ValueError("routing source must be a root-controlled regular file")
    return resolved


def read_plan(path):
    plan = json.loads(path.read_text())
    fields = {"schema_version", "sources", "guard_directory", "allowed_cidrs",
              "hostname", "port", "report_owner", "report_key"}
    if not isinstance(plan, dict) or set(plan) != fields or plan["schema_version"] != 1:
        raise ValueError("unexpected plan")
    if set(plan["sources"]) != {"global", "portal", "service"}:
        raise ValueError("exact three routing sources required")
    for item in plan["sources"].values():
        if set(item) != {"path", "sha256"} or not re.fullmatch(r"[a-f0-9]{64}", item["sha256"]):
            raise ValueError("invalid source digest")
        path = regular(Path(item["path"]))
        if digest(path.read_bytes()) != item["sha256"]:
            raise ValueError("routing source drift")
    guard = Path(plan["guard_directory"])
    if not re.fullmatch(r"/usr/local/etc/nginx/conf\.d/Docker-[a-f0-9]{64}-80", str(guard)):
        raise ValueError("unexpected Web Station user include directory")
    if not isinstance(plan["allowed_cidrs"], list) or not 1 <= len(plan["allowed_cidrs"]) <= 8:
        raise ValueError("bounded CIDR inventory required")
    ranges = [ipaddress.ip_network(x, strict=True) for x in
              ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16",
               "100.64.0.0/10", "127.0.0.1/32", "::1/128")]
    for value in plan["allowed_cidrs"]:
        net = ipaddress.ip_network(value, strict=True)
        if not any(net.version == base.version and net.subnet_of(base) for base in ranges):
            raise ValueError("only bounded private/tailnet/loopback origin ranges allowed")
    if not re.fullmatch(r"[A-Za-z0-9.-]{1,253}", plan["hostname"]):
        raise ValueError("invalid hostname")
    if type(plan["port"]) is not int or not 1 <= plan["port"] <= 65535:
        raise ValueError("invalid port")
    if not re.fullmatch(r"[a-z0-9-]{8,64}", plan["report_key"]):
        raise ValueError("invalid report key")
    pwd.getpwnam(plan["report_owner"])
    return plan


def render(plan, address="$realip_remote_addr"):
    # Original socket peer, never an untrusted forwarded header. nginx realip docs.
    geo = "geo " + address + " $smartbox_ota_origin_allowed {\n  default 0;\n"
    geo += "".join("  " + str(ipaddress.ip_network(x)) + " 1;\n" for x in plan["allowed_cidrs"])
    geo += "}\nmap \"$uri:$smartbox_ota_origin_allowed\" $smartbox_ota_origin_reject {\n"
    geo += "  default 0;\n  \"/firmware.bin:0\" 1;\n}\n"
    guard = "if ($smartbox_ota_origin_reject) { return 403; }\n"
    return geo, guard


def run(args):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=25)
    if result.returncode:
        global FAILED_QUERY
        FAILED_QUERY = {"command": args, "returncode": result.returncode,
                        "stdout": result.stdout.decode("utf-8", "replace")[-4000:],
                        "stderr": result.stderr.decode("utf-8", "replace")[-4000:]}
        raise RuntimeError("nginx validation/reload failed; private report retains diagnostics")
    return result


def create(path, text):
    fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as stream:
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())


def fixture(plan, work):
    # Test address injection exists ONLY in this root-private Unix-socket fixture.
    geo, guard = render(plan, address="$arg_client")
    socket_path = work / "fixture.sock"
    config = work / "fixture.conf"
    temporary_paths = "".join(key + " " + str(work/value) + ";\n"
        for key, value in [("client_body_temp_path", "client-temp"),
                           ("proxy_temp_path", "proxy-temp"),
                           ("fastcgi_temp_path", "fastcgi-temp"),
                           ("uwsgi_temp_path", "uwsgi-temp"),
                           ("scgi_temp_path", "scgi-temp")])
    create(config, "pid " + str(work/"fixture.pid") + ";\n"
           "error_log " + str(work/"fixture.error.log") + ";\n"
           "events { worker_connections 32; }\nhttp {\n access_log off;\n" +
           temporary_paths + geo +
           " server { listen unix:" + str(socket_path) + ";\n" + guard +
           " location / { return 204; }\n }\n}\n")
    with (work/"fixture-process.log").open("wb") as output:
        process = subprocess.Popen([NGINX, "-p", str(work)+"/", "-c", str(config),
                                    "-g", "daemon off;"], stdout=output, stderr=output)
        try:
            deadline = time.monotonic()+5
            while not socket_path.exists():
                if process.poll() is not None or time.monotonic() > deadline:
                    details = "".join(path.read_text(errors="replace")[-2000:]
                        for path in (work/"fixture-process.log", work/"fixture.error.log")
                        if path.exists())
                    raise RuntimeError("private nginx fixture did not start: " + details)
                time.sleep(0.05)
            cases = [("/firmware.bin", "198.51.100.10", 403),
                     ("/firmware.bin", "2001:db8::10", 403),
                     ("/%66irmware.bin", "198.51.100.10", 403),
                     ("//firmware.bin", "198.51.100.10", 403),
                     ("/x/../firmware.bin", "198.51.100.10", 403),
                     ("/firmware.bin", "127.0.0.1", 204),
                     ("/firmware.bin", "::1", 204),
                     ("/version.json", "198.51.100.10", 204),
                     ("/gatekeeper_apk/app.apk", "198.51.100.10", 204),
                     ("/firmware/gatekeeper.sgkenc", "198.51.100.10", 204)]
            for cidr in plan["allowed_cidrs"]:
                cases.append(("/firmware.bin", str(ipaddress.ip_network(cidr).network_address), 204))
            for path, address, expected in cases:
                for method in ("HEAD", "GET"):
                    conn = http.client.HTTPConnection("localhost", timeout=3)
                    conn.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    conn.sock.connect(str(socket_path))
                    conn.request(method, path+"?client="+address,
                                 headers={"X-Forwarded-For": "127.0.0.1",
                                          "X-Real-IP": "127.0.0.1", "Range": "bytes=0-0"})
                    response = conn.getresponse()
                    actual = response.status
                    response.read(128)
                    conn.close()
                    if actual != expected:
                        raise RuntimeError("private origin guard behavior mismatch")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
    return len(cases)*2


def head_local(plan, path):
    connection = http.client.HTTPSConnection(plan["hostname"], plan["port"], timeout=5)
    connection.sock = ssl.create_default_context().wrap_socket(
        socket.create_connection(("127.0.0.1", plan["port"]), timeout=5),
        server_hostname=plan["hostname"])
    connection.request("HEAD", path)
    response = connection.getresponse()
    status = response.status
    connection.close()
    if status != 200:
        raise RuntimeError("preserved strict-TLS local route did not return200")
    return status


def execute(plan, apply):
    output = Path("/var/run/sgk-broker-audit")/(plan["report_key"] +
              ("-apply.json" if apply else "-validation.json"))
    if output.exists() or output.is_symlink():
        raise ValueError("report already exists; refusing to repeat operation")
    base = Path("/volume1/smart-gatekeeper-broker-private")
    if not base.is_dir() or base.is_symlink() or base.stat().st_uid != 0:
        raise ValueError("verified root-private backup directory required")
    work = Path(tempfile.mkdtemp(prefix="ota-origin-", dir=str(base)))
    os.chmod(str(work), 0o700)
    files = {}
    for role, item in plan["sources"].items():
        files[role] = Path(item["path"]).read_text()
        create(work/(role+".original.conf"), files[role])
    hook = "include "+plan["guard_directory"]+"/user.conf*;"
    if files["service"].count(hook) != 1:
        raise ValueError("service does not include the selected persistent user hook")
    if (Path("/etc/nginx/conf.d").resolve() != HTTP_INCLUDE.parent.resolve()
            or HTTP_INCLUDE.parent.stat().st_uid != 0
            or HTTP_INCLUDE.parent.stat().st_mode & 0o022):
        raise ValueError("unexpected HTTP include directory")
    geo, guard = render(plan)
    create(work/"geo.candidate.conf", geo)
    create(work/"guard.candidate.conf", guard)
    # Validate the entire current routing tree with private candidate copies.
    service_path = plan["sources"]["service"]["path"]
    portal_path = plan["sources"]["portal"]["path"]
    service_copy = work/"service.candidate.conf"
    create(service_copy, files["service"]+"\ninclude "+str(work/"guard.candidate.conf")+";\n")
    needle = "include conf.d/"+Path(service_path).name+"*;"
    if files["portal"].count(needle) != 1:
        raise ValueError("portal service include does not match the pinned source")
    create(work/"portal.candidate.conf", files["portal"].replace(
        needle, "include "+str(service_copy)+";"))
    glob = "include sites-enabled/*;"
    http = "include conf.d/http.*.conf;"
    if files["global"].count(glob) != 1 or files["global"].count(http) != 1:
        raise ValueError("unexpected global include layout")
    site_lines = []
    portal_resolved = Path(portal_path).resolve()
    for path in sorted(Path("/etc/nginx/sites-enabled").iterdir()):
        if path.resolve() == portal_resolved:
            site_lines.append("include "+str(work/"portal.candidate.conf")+";")
        elif path.is_file():
            site_lines.append("include "+str(path)+";")
    candidate = files["global"].replace(glob, "\n".join(site_lines)).replace(
        http, http+"\ninclude "+str(work/"geo.candidate.conf")+";")
    create(work/"global.candidate.conf", candidate)
    # nginx resolves every relative include against the main config's directory,
    # including includes inside existing vhosts. -p does not change that prefix.
    # This unreferenced root0600 temporary file is not part of any live include.
    fd, validation_name = tempfile.mkstemp(prefix=".smartbox-ota-validation-", dir="/etc/nginx")
    try:
        with os.fdopen(fd, "w") as stream:
            stream.write(candidate)
        run([NGINX, "-t", "-p", "/etc/nginx/", "-c", validation_name])
    finally:
        Path(validation_name).unlink()
    cases = fixture(plan, work)
    for path in ("/firmware.bin", "/version.json"):
        head_local(plan, path)
    report = {"schema_version": 1, "captured_at_utc": datetime.now(timezone.utc).isoformat(),
              "private_behavior_cases": cases, "full_candidate_config_valid": True,
              "operating_configuration_changed": False, "external_refusal_verified": False,
              "credentials_changed": False, "backup_root": str(work)}
    if apply:
        # Recheck live source bytes immediately before creating new include files.
        read_plan_from_sources(plan)
        directory = Path(plan["guard_directory"])
        if not directory.exists():
            directory.mkdir(mode=0o755)
        if directory.is_symlink() or directory.stat().st_uid != 0 or directory.stat().st_mode & 0o022:
            raise ValueError("unsafe user include directory")
        targets = [(HTTP_INCLUDE, geo), (directory/"user.conf.smartbox_ota_origin_guard", guard)]
        if any(path.exists() or path.is_symlink() for path, _ in targets):
            raise ValueError("guard file already exists; explicit upgrade required")
        written = []
        try:
            for path, content in targets:
                create(path, content)
                written.append((path, digest(content.encode())))
            run([NGINX, "-t"])
            run([NGINX, "-s", "reload"])
            time.sleep(0.5)
            head_local(plan, "/firmware.bin")
            head_local(plan, "/version.json")
            report["operating_configuration_changed"] = True
            report["installed_file_hashes"] = {str(p): h for p, h in written}
        except Exception:
            for path, expected in reversed(written):
                if path.is_symlink() or digest(path.read_bytes()) != expected:
                    raise RuntimeError("concurrent guard mutation; refusing unsafe rollback")
                path.unlink()
            run([NGINX, "-t"])
            run([NGINX, "-s", "reload"])
            raise
    create(work/"evidence.json", json.dumps(report, sort_keys=True))
    create(output, json.dumps(report, sort_keys=True))
    owner = pwd.getpwnam(plan["report_owner"])
    os.chown(str(output), owner.pw_uid, owner.pw_gid)
    print(json.dumps({k: v for k, v in report.items()
                      if k not in ("backup_root", "installed_file_hashes")}, sort_keys=True))


def read_plan_from_sources(plan):
    for item in plan["sources"].values():
        if digest(regular(Path(item["path"])).read_bytes()) != item["sha256"]:
            raise ValueError("routing source drift before activation")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("root is required for full nginx validation and private backup")
    plan = read_plan(args.plan)
    try:
        execute(plan, args.apply)
    except Exception as error:
        output = Path("/var/run/sgk-broker-audit")/(plan["report_key"]+"-error.json")
        create(output, json.dumps({"error_type": type(error).__name__,
               "message": str(error), "failed_query": FAILED_QUERY}, sort_keys=True))
        owner = pwd.getpwnam(plan["report_owner"])
        os.chown(str(output), owner.pw_uid, owner.pw_gid)
        print("origin_guard_failed=true; private_error_report_saved=true")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
