#!/usr/bin/env python3
"""Add one narrow private LAN to an installed guard with pinned rollback."""
import argparse
from datetime import datetime, timezone
import ipaddress
import json
import os
from pathlib import Path
import pwd
import stat
import tempfile
import time
try:
    from . import install_ota_origin_guard as guard
except ImportError:
    import install_ota_origin_guard as guard

OPS_CONFIG = Path("/usr/local/lib/smart-gatekeeper-broker-ops/config.json")
PRIVATE = Path("/volume1/smart-gatekeeper-broker-private")
MAIN_CONFIG_DIRECTORY = Path("/etc/nginx")
REPORT_DIRECTORY = Path("/var/run/sgk-broker-audit")


def protected_directory(path, mask):
    info = path.stat()
    if path.is_symlink() or not path.is_dir() or info.st_uid != 0 or info.st_mode & mask:
        raise ValueError("protected root directory required")


def add_lan(origin, cidr):
    network = ipaddress.ip_network(cidr, strict=True)
    private = [ipaddress.ip_network(x) for x in
               ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")]
    if network.version != 4 or network.prefixlen < 24 or not any(
            network.subnet_of(base) for base in private):
        raise ValueError("only one IPv4 private /24 or narrower LAN may be added")
    if cidr in origin["allowed_cidrs"] or len(origin["allowed_cidrs"]) >= 8:
        raise ValueError("LAN already present or range budget exhausted")
    return {**origin, "allowed_cidrs": [*origin["allowed_cidrs"], str(network)]}


def expand_http_includes(global_text, paths, replaced, candidate):
    marker = "include conf.d/http.*.conf;"
    paths = list(paths)
    if global_text.count(marker) != 1 or paths.count(replaced) != 1:
        raise ValueError("unexpected live HTTP include layout")
    replacement = "\n".join("include " + str(candidate if p == replaced else p) + ";"
                            for p in paths)
    return global_text.replace(marker, replacement)


def replace(path, content, original_info):
    fd, name = tempfile.mkstemp(prefix=".origin-update-", dir=str(path.parent))
    try:
        os.fchmod(fd, stat.S_IMODE(original_info.st_mode))
        os.fchown(fd, original_info.st_uid, original_info.st_gid)
        with os.fdopen(fd, "wb") as stream:
            stream.write(content);stream.flush();os.fsync(stream.fileno())
        os.replace(name, str(path))
    finally:
        if os.path.exists(name):os.unlink(name)


def execute(update, apply):
    if set(update) != {"origin_plan", "expected_guard_hashes", "additional_lan_cidr"}:
        raise ValueError("unexpected update fields")
    if os.geteuid() != 0:
        raise ValueError("administrator authentication required")
    guard.regular(OPS_CONFIG)
    protected_directory(PRIVATE, 0o077)
    protected_directory(guard.HTTP_INCLUDE.parent, 0o022)
    work = Path(tempfile.mkdtemp(prefix="ota-network-", dir=str(PRIVATE)))
    os.chmod(str(work), 0o700)
    guard.create(work/"origin-plan.json", json.dumps(update["origin_plan"]))
    origin = guard.read_plan(work/"origin-plan.json")
    candidate_plan = add_lan(origin, update["additional_lan_cidr"])
    paths = [guard.HTTP_INCLUDE, Path(origin["guard_directory"])/"user.conf.smartbox_ota_origin_guard"]
    expected = update["expected_guard_hashes"]
    if set(expected) != set(map(str, paths)):
        raise ValueError("exact installed guard hashes required")
    originals = {p: guard.regular(p).read_bytes() for p in paths}
    if any(guard.digest(originals[p]) != expected[str(p)] for p in paths):
        raise ValueError("installed guard drift")
    config_bytes = OPS_CONFIG.read_bytes()
    config = json.loads(config_bytes)
    if config["owner"] != origin["report_owner"] or config["origin_guard_hashes"] != expected:
        raise ValueError("gateway origin binding drift")
    geo, unchanged_guard = guard.render(candidate_plan)
    if unchanged_guard.encode() != originals[paths[1]]:
        raise ValueError("update must preserve the path guard exactly")
    old_geo, old_guard = guard.render(origin)
    if old_geo.encode() != originals[paths[0]] or old_guard.encode() != originals[paths[1]]:
        raise ValueError("installed guard does not match original plan")
    candidate_file = work/"geo.candidate.conf"
    guard.create(candidate_file, geo)
    global_text = Path(origin["sources"]["global"]["path"]).read_text()
    includes = sorted(guard.HTTP_INCLUDE.parent.glob("http.*.conf"))
    candidate = expand_http_includes(global_text, includes, paths[0], candidate_file)
    fd, validation = tempfile.mkstemp(prefix=".smartbox-network-validation-", dir=str(MAIN_CONFIG_DIRECTORY))
    try:
        with os.fdopen(fd, "w") as stream:stream.write(candidate)
        guard.run([guard.NGINX, "-t", "-p", "/etc/nginx/", "-c", validation])
    finally:
        Path(validation).unlink()
    cases = guard.fixture(candidate_plan, work)
    for path in ("/firmware.bin", "/version.json"):guard.head_local(origin, path)
    report_path = REPORT_DIRECTORY/(origin["report_key"]+"-update.json")
    if report_path.exists() or report_path.is_symlink():
        raise ValueError("report exists; refusing repeat")
    report = {"schema_version": 1, "captured_at_utc": datetime.now(timezone.utc).isoformat(),
              "private_behavior_cases": cases, "full_candidate_config_valid": True,
              "operating_configuration_changed": False, "credentials_changed": False,
              "external_refusal_verified": False, "device_download_verified": False}
    if apply:
        guard.read_plan_from_sources(origin)
        if OPS_CONFIG.read_bytes() != config_bytes or any(
                p.read_bytes() != originals[p] for p in paths):
            raise ValueError("concurrent guard or gateway mutation")
        for p in paths:guard.create(work/(p.name+".original"), originals[p].decode())
        guard.create(work/"gateway-config.original.json", config_bytes.decode())
        geo_info = paths[0].stat();config_info = OPS_CONFIG.stat()
        updated = {**config, "origin_guard_hashes":
                   {**expected, str(paths[0]): guard.digest(geo.encode())}}
        new_config = json.dumps(updated, sort_keys=True).encode()
        changed_config = False
        try:
            replace(paths[0], geo.encode(), geo_info)
            guard.run([guard.NGINX, "-t"]);guard.run([guard.NGINX, "-s", "reload"])
            time.sleep(0.5)
            for path in ("/firmware.bin", "/version.json"):guard.head_local(origin, path)
            if OPS_CONFIG.read_bytes() != config_bytes:
                raise RuntimeError("gateway changed during activation")
            replace(OPS_CONFIG, new_config, config_info);changed_config = True
            report["operating_configuration_changed"] = True
            report["installed_file_hashes"] = updated["origin_guard_hashes"]
        except Exception:
            if paths[0].read_bytes() != geo.encode():
                raise RuntimeError("concurrent guard mutation prevents safe rollback")
            replace(paths[0], originals[paths[0]], geo_info)
            if changed_config:
                if OPS_CONFIG.read_bytes() != new_config:
                    raise RuntimeError("concurrent gateway mutation prevents safe rollback")
                replace(OPS_CONFIG, config_bytes, config_info)
            guard.run([guard.NGINX, "-t"]);guard.run([guard.NGINX, "-s", "reload"])
            raise
    guard.create(work/"evidence.json", json.dumps(report, sort_keys=True))
    guard.create(report_path, json.dumps(report, sort_keys=True))
    owner = pwd.getpwnam(origin["report_owner"])
    os.chown(str(report_path), owner.pw_uid, owner.pw_gid)
    print(json.dumps({k: v for k, v in report.items() if k != "installed_file_hashes"}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--expected-plan-sha256", required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    with args.plan.open("rb") as stream:raw = stream.read(131073)
    if len(raw) > 131072 or guard.digest(raw) != args.expected_plan_sha256:
        raise ValueError("pinned update plan mismatch")
    execute(json.loads(raw), args.apply)


if __name__ == "__main__":
    main()
