import base64
import hashlib
import importlib
import json
import subprocess
import tempfile
from unittest.mock import patch
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))

def module():
    return importlib.import_module('publish_private_ota')

def pin(byte=b'k'):
    def field(data): return struct.pack('>I', len(data)) + data
    return 'ssh-ed25519 ' + base64.b64encode(field(b'ssh-ed25519') + field(byte*32)).decode()

def fixture(**options):
    env = dict(SMARTBOX_SFTP_HOST='100.64.1.2', SMARTBOX_SFTP_PORT='4222',
               SMARTBOX_SFTP_USER='publisher', SMARTBOX_SFTP_PASSWORD='fixture', SMARTBOX_SFTP_TARGET_DIR='/web/ota',
               SMARTBOX_SFTP_HOST_KEY=pin(), SMARTBOX_SFTP_STAGE_DIR='/private/ota-stage',
               SMARTBOX_SFTP_ROLLBACK_DIR='/private/ota-backup')
    env.update(options)
    return env

class PrivatePublishingTests(unittest.TestCase):
    def test_rejects_non_private_address(self):
        for addr in ('example.com', '192.0.2.5', '100.63.255.255', '100.128.0.0'):
            with self.subTest(addr=addr), self.assertRaises(ValueError):
                module().configuration(fixture(SMARTBOX_SFTP_HOST=addr))

    def test_requires_valid_host_pin(self):
        for key in ('', 'ssh-ed25519 YQ==', pin()+'\n* ssh-ed25519 A'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                module().configuration(fixture(SMARTBOX_SFTP_HOST_KEY=key))

    def test_rejects_untrusted_path_user_and_port(self):
        for k,v in [('SMARTBOX_SFTP_TARGET_DIR','/ota\nput ./secret'), ('SMARTBOX_SFTP_TARGET_DIR','/web/../admin'),
                    ('SMARTBOX_SFTP_USER','user@host'), ('SMARTBOX_SFTP_PORT','22 -oProxyCommand=x'),
                    ('SMARTBOX_SFTP_PORT','65536'), ('SMARTBOX_SFTP_PASSWORD',''), ('SMARTBOX_SFTP_STAGE_DIR','/web/ota/hidden'),
                    ('SMARTBOX_SFTP_STAGE_DIR','/web')]:
            with self.subTest(k=k), self.assertRaises(ValueError):
                module().configuration(fixture(**{k:v}))

    def test_read_only_preflight_checks_canonical_paths(self):
        m = module()
        endpoint = m.configuration(fixture())
        expected = 'cd /web/ota\npwd\ncd /private/ota-stage\npwd\ncd /private/ota-backup\npwd\n'
        self.assertEqual(m.read_only_batch(endpoint), expected)
        result = '\n'.join('Remote working directory: ' + path for path in
                            ('/web/ota', '/private/ota-stage', '/private/ota-backup'))
        m.preflight(endpoint, lambda *_: result)
        with self.assertRaises(ValueError):
            m.preflight(endpoint, lambda *_: result.replace('/private/ota-backup', '/web'))

    def test_publish_verifies_new_image_before_discovery_and_reserves_once(self):
        self.exercise_publication()

    def test_failed_manifest_promotion_restores_old_pair(self):
        self.exercise_publication(failure=True)

    def test_lost_withdrawal_ack_restores_discovery(self):
        self.exercise_publication(lost_withdrawal_ack=True)

    def exercise_publication(self, failure=False, lost_withdrawal_ack=False):
        m = module()
        commit = 'a' * 40
        endpoint = m.configuration(fixture())
        target, stage, rollback = endpoint.target, endpoint.stage, endpoint.rollback
        old, new = b'old image', b'new image'
        def digest(image):
            return hashlib.sha256(image).hexdigest()

        def manifest(version, image):
            return json.dumps({'version': version, 'commit': commit, 'size': len(image),
                               'sha256': digest(image)}).encode()

        remote = {f'{target}/firmware.bin': old, f'{target}/version.json': manifest('old', old)}
        dirs = {target, stage, rollback}
        events = []
        def run(_, commands, cwd=None):
            current = None
            result = []
            for line in commands.splitlines():
                verb, *args = line.split()
                events.append((verb, *args))
                if verb == 'cd':
                    self.assertIn(args[0], dirs)
                    current = args[0]
                elif verb == 'pwd':
                    result.append('Remote working directory: ' + current)
                elif verb == 'mkdir':
                    if args[0] in dirs:
                        raise subprocess.CalledProcessError(1, 'sftp')
                    dirs.add(args[0])
                elif verb == 'get':
                    (cwd / args[1]).write_bytes(remote[args[0]])
                elif verb == 'put':
                    remote[args[1]] = (cwd / args[0]).read_bytes()
                elif verb == 'rename':
                    if (failure and args[0].startswith(stage)
                            and args[0].endswith('/version.json')):
                        raise subprocess.CalledProcessError(1, 'sftp')
                    if args[0] not in remote:
                        raise subprocess.CalledProcessError(1, 'sftp')
                    remote[args[1]] = remote.pop(args[0])
                    if lost_withdrawal_ack and args[1].endswith('/withdrawn.json'):
                        raise subprocess.CalledProcessError(1, 'sftp')
            return '\n'.join(result)

        def verify(version, sha):
            if f'{target}/version.json' not in remote:
                raise subprocess.CalledProcessError(1, 'verify HTTPS')
            self.assertEqual(json.loads(remote[f'{target}/version.json'])['version'], version)
            self.assertEqual(digest(remote[f'{target}/firmware.bin']), sha)

        def withdrawn():
            self.assertNotIn(f'{target}/version.json', remote)

        def verify_image(sha, size):
            withdrawn()
            image = remote[f'{target}/firmware.bin']
            self.assertEqual((digest(image), len(image)), (sha, size))

        with tempfile.TemporaryDirectory() as tmp:
            dist = Path(tmp)
            (dist / 'firmware.bin').write_bytes(new)
            (dist / 'version.json').write_bytes(manifest('new', new))
            with patch.object(m.Path, 'resolve', return_value=dist):
                args = (endpoint, commit, run, verify, withdrawn, verify_image)
                if failure:
                    with self.assertRaisesRegex(ValueError, 'restored and verified'):
                        m.publish(*args)
                    verify('old', digest(old))
                elif lost_withdrawal_ack:
                    with self.assertRaisesRegex(ValueError, 'before image promotion'):
                        m.publish(*args)
                    verify('old', digest(old))
                    self.assertFalse(any(event[0] == 'rename' and event[-1] == f'{target}/firmware.bin'
                                         for event in events))
                else:
                    m.publish(*args)
                    verify('new', digest(new))
                self.assertEqual(remote[f'{rollback}/release-{commit}/firmware.bin'], old)
                writes = sum(name == 'rename' for name, *_ in events)
                with self.assertRaises(subprocess.CalledProcessError):
                    m.publish(*args)
                self.assertEqual(sum(name == 'rename' for name, *_ in events), writes)

    def test_preflight_proof_is_scoped_to_readonly_step_and_commit(self):
        m = importlib.import_module('verify_private_preflight_run')
        head = 'a' * 40
        run = {'head_sha': head, 'head_branch': 'main', 'event': 'workflow_dispatch',
               'conclusion': 'success', 'path': '.github/workflows/deploy.yml'}
        job = {'name': m.JOB, 'conclusion': 'success',
               'steps': [{'name': m.STEP, 'conclusion': 'success'}]}
        self.assertTrue(m.accepted(run, [job], head))
        self.assertFalse(m.accepted(run, [job], 'b' * 40))
        self.assertFalse(m.accepted(run, [job | {'steps': []}], head))

    def test_exact_readback_requires_manifest_to_match_image(self):
        origin = importlib.import_module('verify_authenticated_ota_origin')
        good = {'version':'0.2.9-g1234567', 'sha256':'a'*64, 'size':42}
        origin.verify_expected_identity(good, 'a'*64, 42, good['version'], 'a'*64)
        for changes in ({'sha256':'b'*64}, {'size':41}, {'version':'other'}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                origin.verify_expected_identity(good | changes, 'a'*64, 42,
                                                good['version'], 'a'*64)

    def test_requires_non_overlapping_private_rollback(self):
        m = module()
        with self.assertRaises(ValueError):
            m.configuration(fixture(SMARTBOX_SFTP_ROLLBACK_DIR='/web/ota/backup'))

    def test_publication_requires_prior_readonly_run_and_no_push(self):
        workflow = (Path(__file__).resolve().parents[1] / '.github/workflows/deploy.yml').read_text()
        job = workflow.split('  build_and_deploy:', 1)[1].split('  prepare_field_recovery:', 1)[0]
        self.assertNotIn("github.event_name == 'push'", job)
        self.assertIn('preflight_run_id', job)
        self.assertIn('use-cache: false', job)
        self.assertIn('secrets.SMARTBOX_SFTP_PASSWORD', job)
        self.assertNotIn('secrets.NAS_PASSWORD', job)

    def test_workflow_is_main_only_and_private_without_fallback(self):
        workflow = (Path(__file__).resolve().parents[1] / '.github/workflows/deploy.yml').read_text()
        self.assertNotIn('wlixcc/SFTP-Deploy-Action', workflow)
        self.assertIn('environment: smartbox-private-ota', workflow)
        self.assertIn('id-token: write', workflow)
        self.assertIn("github.ref == 'refs/heads/main'", workflow)
        self.assertIn('publish_private_ota.py preflight', workflow)
        self.assertIn('publish_private_ota.py publish', workflow)
        self.assertIn('EXPECTED_SHA256', workflow)

if __name__=='__main__': unittest.main()
