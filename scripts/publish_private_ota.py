#!/usr/bin/env python3
"""Strict, private-only SFTP; credential-bearing files never enter Actions artifacts."""
import base64
import binascii
from dataclasses import dataclass
import hashlib
import ipaddress
from itertools import combinations
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import tempfile


@dataclass(frozen=True, repr=False)
class Endpoint:
    host: str
    port: str
    user: str
    password: str
    key: str
    target: str
    stage: str
    rollback: str


def checked_path(value):
    if not re.fullmatch(r'(/[A-Za-z0-9_.-]+)+', value) or any(
        part in ('.', '..') for part in value.split('/')
    ):
        raise ValueError('invalid SFTP directory')
    return PurePosixPath(value)


def configuration(env):
    values = [env.get(k, '') for k in (
        'SMARTBOX_SFTP_HOST', 'SMARTBOX_SFTP_PORT', 'SMARTBOX_SFTP_USER', 'SMARTBOX_SFTP_PASSWORD',
        'SMARTBOX_SFTP_HOST_KEY', 'SMARTBOX_SFTP_TARGET_DIR', 'SMARTBOX_SFTP_STAGE_DIR',
        'SMARTBOX_SFTP_ROLLBACK_DIR')]
    e = Endpoint(*values)
    try:
        private = ipaddress.IPv4Address(e.host) in ipaddress.IPv4Network('100.64.0.0/10')
    except ipaddress.AddressValueError:
        private = False
    if not private:
        raise ValueError('an explicit Tailscale IPv4 address is required')
    if not re.fullmatch(r'[1-9][0-9]{0,4}', e.port) or int(e.port) > 65535:
        raise ValueError('invalid private SFTP port')
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}', e.user):
        raise ValueError('invalid SFTP principal')
    if not e.password or any(c in e.password for c in '\r\n\0'):
        raise ValueError('missing or invalid SFTP credential')
    match = re.fullmatch(r'ssh-ed25519 ([A-Za-z0-9+/]+={0,2})', e.key)
    try:
        raw = base64.b64decode(match.group(1), validate=True) if match else b''
    except binascii.Error:
        raw = b''
    prefix = struct.pack('>I', 11) + b'ssh-ed25519' + struct.pack('>I', 32)
    if not raw.startswith(prefix) or len(raw) != len(prefix)+32:
        raise ValueError('a trusted NAS Ed25519 host-key pin is required')
    paths = [checked_path(value) for value in (e.target, e.stage, e.rollback)]
    for left, right in combinations(paths, 2):
        if left == right or left in right.parents or right in left.parents:
            raise ValueError('publish, staging and rollback directories must not overlap')
    return e


def read_only_batch(endpoint):
    paths = [checked_path(path) for path in (endpoint.target, endpoint.stage, endpoint.rollback)]
    return ''.join(f'cd {path}\npwd\n' for path in paths)


def transfer(e, commands, cwd=None):
    with tempfile.TemporaryDirectory(prefix='smartbox-sftp-') as tmp:
        root = Path(tmp)
        known = root / 'known_hosts'
        host_pattern = e.host if e.port == '22' else f'[{e.host}]:{e.port}'
        known.write_text(f'{host_pattern} {e.key}\n', encoding='ascii')
        askpass = root / 'askpass'
        askpass.write_text('#!/bin/sh\nprintf "%s\\n" "$NAS_PASSWORD"\n', encoding='ascii')
        askpass.chmod(0o700)
        cmd = ['sftp', '-F', '/dev/null', '-oBatchMode=no', '-b', '-',
               f'-oUserKnownHostsFile={known}', '-oGlobalKnownHostsFile=/dev/null',
               '-oStrictHostKeyChecking=yes', '-oHostKeyAlgorithms=ssh-ed25519',
               '-oPreferredAuthentications=password', '-oPubkeyAuthentication=no',
               '-oNumberOfPasswordPrompts=1', '-oConnectTimeout=15', '-P', e.port,
               f'{e.user}@{e.host}']
        env = {'PATH': os.defpath, 'LC_ALL': 'C', 'SSH_ASKPASS': str(askpass),
               'SSH_ASKPASS_REQUIRE': 'force', 'NAS_PASSWORD': e.password}
        result = subprocess.run(cmd, env=env, input=commands, text=True, cwd=cwd,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                timeout=180, check=True, start_new_session=True)
        return result.stdout


def preflight(endpoint, run=transfer):
    result = run(endpoint, read_only_batch(endpoint))
    observed = re.findall(r'^Remote working directory: (.+)$', result, re.MULTILINE)
    if observed != [endpoint.target, endpoint.stage, endpoint.rollback]:
        raise ValueError('SFTP canonical paths do not match the configured directories')


def checked_pair(folder, image, metadata, expected_commit=None):
    manifest_file, firmware_file = folder / metadata, folder / image
    if not 0 < manifest_file.stat().st_size <= 65536 or not 0 < firmware_file.stat().st_size <= 8388608:
        raise ValueError('OTA pair has invalid file sizes')
    try:
        manifest = json.loads(manifest_file.read_text())
    except (UnicodeError, json.JSONDecodeError):
        raise ValueError('OTA manifest invalid') from None
    if not isinstance(manifest, dict):
        raise ValueError('OTA manifest invalid')
    version = manifest.get('version', '')
    sha = hashlib.sha256(firmware_file.read_bytes()).hexdigest()
    if (not isinstance(version, str) or not re.fullmatch(r'[A-Za-z0-9.+_-]{1,96}', version) or
            manifest.get('sha256') != sha or
            type(manifest.get('size')) is not int or manifest['size'] != firmware_file.stat().st_size):
        raise ValueError('OTA pair failed local version, size or digest validation')
    if expected_commit and manifest.get('commit') != expected_commit:
        raise ValueError('OTA pair does not match the authorized commit')
    return version, sha


def verify_https(version, digest):
    verifier = Path(__file__).with_name('verify_authenticated_ota_origin.py')
    subprocess.run([sys.executable, str(verifier)],
                   env={**os.environ, 'EXPECTED_VERSION': version, 'EXPECTED_SHA256': digest},
                   timeout=180, check=True)


def manifest_is_withdrawn():
    from verify_authenticated_ota_origin import authenticated_access, request
    opener, manifest_url, _, auth, _ = authenticated_access()
    code, _ = request(opener, manifest_url, 'GET', auth)
    if code not in (404, 410):
        raise ValueError('authenticated HTTPS manifest is still available')


def expected_image_is_served(digest, size):
    from verify_authenticated_ota_origin import authenticated_access, request
    opener, _, firmware_url, auth, wrong = authenticated_access()
    for identity in (None, wrong):
        code, _ = request(opener, firmware_url, 'HEAD', identity)
        if code not in (401, 403):
            raise ValueError('firmware origin refused to enforce HTTPS authentication')
    code, image = request(opener, firmware_url, 'GET', auth)
    if code != 200 or len(image) != size or hashlib.sha256(image).hexdigest() != digest:
        raise ValueError('authenticated image failed verification before discovery')


def publish(endpoint, commit, run=transfer, verify=verify_https,
            withdrawn=manifest_is_withdrawn, verify_image=expected_image_is_served):
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('a full reviewed commit is required')
    preflight(endpoint, run)
    local = Path('dist').resolve()
    new_pair = checked_pair(local, 'firmware.bin', 'version.json', commit)
    target = endpoint.target
    reservation = f'{endpoint.rollback}/release-{commit}'
    stage = f'{endpoint.stage}/release-{commit}'

    def perform(commands, cwd=None):
        return run(endpoint, '\n'.join(commands) + '\n', cwd)

    with tempfile.TemporaryDirectory(prefix='smartbox-rollback-check-') as tmp:
        folder = Path(tmp)
        perform([f'get {target}/firmware.bin old.bin',
                 f'get {target}/version.json old.json'], folder)
        old_pair = checked_pair(folder, 'old.bin', 'old.json')
        verify(*old_pair)
        # Server-side mkdir fails if this exact commit has already entered publication.
        perform([f'mkdir {reservation}', f'mkdir {stage}'])
        perform([f'put old.bin {reservation}/firmware.bin',
                 f'put old.json {reservation}/version.json',
                 f'get {reservation}/firmware.bin backup.bin',
                 f'get {reservation}/version.json backup.json'], folder)
        if checked_pair(folder, 'backup.bin', 'backup.json') != old_pair:
            raise ValueError('remote rollback copy did not verify')
        perform([f'put firmware.bin {stage}/firmware.bin',
                 f'put version.json {stage}/version.json'], local)
        perform([f'get {stage}/firmware.bin staged.bin',
                 f'get {stage}/version.json staged.json'], folder)
        if checked_pair(folder, 'staged.bin', 'staged.json', commit) != new_pair:
            raise ValueError('remote staging copy did not verify')
        # Check the old pair again immediately before withdrawing discovery.
        verify(*old_pair)
        try:
            perform([f'rename {target}/version.json {reservation}/withdrawn.json'])
        except (OSError, subprocess.SubprocessError):
            # A disconnected client cannot tell whether the server completed the rename.
            try:
                verify(*old_pair)
            except (OSError, subprocess.SubprocessError):
                try:
                    withdrawn()
                    perform([f'put old.json {stage}/restore.json',
                             f'rename {stage}/restore.json {target}/version.json'], folder)
                    verify(*old_pair)
                except (ValueError, OSError, subprocess.SubprocessError):
                    raise ValueError(
                        'initial manifest restore unconfirmed; inspect private rollback') from None
            raise ValueError('publication stopped before image promotion; previous pair verified') from None
        manifest_promoted = False
        try:
            withdrawn()
            perform([f'rename {stage}/firmware.bin {target}/firmware.bin'])
            verify_image(new_pair[1], (local / 'firmware.bin').stat().st_size)
            # Mark before sending, since a broken connection can hide a successful rename.
            manifest_promoted = True
            perform([f'rename {stage}/version.json {target}/version.json'])
            verify(*new_pair)
        except (ValueError, OSError, subprocess.SubprocessError):
            try:
                if manifest_promoted:
                    try:
                        perform([f'rename {target}/version.json {reservation}/rejected.json'])
                    except subprocess.SubprocessError:
                        pass
                withdrawn()
                # Keep the separately verified backup and restore from temporary staged copies.
                perform([f'put old.bin {stage}/restore.bin', f'put old.json {stage}/restore.json',
                         f'rename {stage}/restore.bin {target}/firmware.bin'], folder)
                verify_image(old_pair[1], (folder / 'old.bin').stat().st_size)
                perform([f'rename {stage}/restore.json {target}/version.json'])
                verify(*old_pair)
            except (ValueError, OSError, subprocess.SubprocessError):
                raise ValueError('automatic restore unconfirmed; inspect private rollback before retrying') from None
            raise ValueError('publication failed; previous pair restored and verified') from None


def main():
    try:
        mode, = sys.argv[1:]
        endpoint = configuration(os.environ)
        if mode == 'preflight':
            preflight(endpoint)
        elif mode == 'publish':
            publish(endpoint, os.environ.get('GITHUB_SHA', ''))
        else:
            raise ValueError('invalid transfer mode')
    except ValueError as exc:
        # ValueError messages above contain no connection parameters.
        print(f'SmartBox private SFTP configuration rejected: {exc}', file=sys.stderr)
        return 1
    except (OSError, subprocess.SubprocessError):
        print('SmartBox private SFTP failed; no public fallback.', file=sys.stderr)
        return 1
    print(f'SmartBox private SFTP {mode} passed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
