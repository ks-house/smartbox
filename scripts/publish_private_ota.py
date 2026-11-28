#!/usr/bin/env python3
"""Strict, private-only SFTP; credential-bearing files never enter Actions artifacts."""
import base64
import binascii
from dataclasses import dataclass
import ipaddress
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


def checked_path(value):
    if not re.fullmatch(r'(/[A-Za-z0-9_.-]+)+', value) or any(
        part in ('.', '..') for part in value.split('/')
    ):
        raise ValueError('invalid SFTP directory')
    return PurePosixPath(value)


def configuration(env):
    values = [env.get(k, '') for k in (
        'SMARTBOX_SFTP_HOST', 'SMARTBOX_SFTP_PORT', 'NAS_USER', 'NAS_PASSWORD',
        'SMARTBOX_SFTP_HOST_KEY', 'NAS_TARGET_DIR', 'SMARTBOX_SFTP_STAGE_DIR')]
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
    target, stage = checked_path(e.target), checked_path(e.stage)
    if target == stage or target in stage.parents or stage in target.parents:
        raise ValueError('staging must not overlap the publish directory')
    return e


def batch(target, stage, mode, nonce=''):
    # Validate again at the boundary where strings become SFTP commands.
    checked_path(target)
    checked_path(stage)
    if mode == 'preflight':
        return f'cd {target}\npwd\ncd {stage}\npwd\n'
    if mode != 'publish' or not re.fullmatch(r'[1-9][0-9]*-[1-9][0-9]*', nonce):
        raise ValueError('invalid publish mode or run identity')
    image = f'{stage}/.firmware.{nonce}.tmp'
    metadata = f'{stage}/.version.{nonce}.tmp'
    return (f'cd {target}\nput firmware.bin {image}\nput version.json {metadata}\n'
            f'rename {image} firmware.bin\nrename {metadata} version.json\n')


def transfer(e, mode, nonce):
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
        subprocess.run(cmd, env=env, input=batch(e.target, e.stage, mode, nonce),
                       text=True, cwd='dist' if mode == 'publish' else None,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=180, check=True, start_new_session=True)


def main():
    try:
        mode, = sys.argv[1:]
        endpoint = configuration(os.environ)
        nonce = f"{os.environ.get('GITHUB_RUN_ID','')}-{os.environ.get('GITHUB_RUN_ATTEMPT','')}"
        transfer(endpoint, mode, nonce)
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
