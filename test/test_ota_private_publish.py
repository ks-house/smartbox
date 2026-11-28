import base64
import importlib
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
               NAS_USER='publisher', NAS_PASSWORD='fixture', NAS_TARGET_DIR='/web/ota',
               SMARTBOX_SFTP_HOST_KEY=pin(), SMARTBOX_SFTP_STAGE_DIR='/private/ota-stage')
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
        for k,v in [('NAS_TARGET_DIR','/ota\nput ./secret'), ('NAS_TARGET_DIR','/web/../admin'),
                    ('NAS_USER','user@host'), ('SMARTBOX_SFTP_PORT','22 -oProxyCommand=x'),
                    ('SMARTBOX_SFTP_PORT','65536'), ('NAS_PASSWORD',''), ('SMARTBOX_SFTP_STAGE_DIR','/web/ota/hidden'),
                    ('SMARTBOX_SFTP_STAGE_DIR','/web')]:
            with self.subTest(k=k), self.assertRaises(ValueError):
                module().configuration(fixture(**{k:v}))

    def test_read_only_batch_and_immutable_publish_names(self):
        m = module()
        self.assertEqual(m.batch('/web/ota','/private/ota-stage','preflight'),
                         'cd /web/ota\npwd\ncd /private/ota-stage\npwd\n')
        lines=m.batch('/web/ota','/private/ota-stage','publish','456-2').splitlines()
        self.assertEqual(lines[0], 'cd /web/ota')
        self.assertEqual(lines[1:3], ['put firmware.bin /private/ota-stage/.firmware.456-2.tmp',
                                      'put version.json /private/ota-stage/.version.456-2.tmp'])
        self.assertEqual(lines[-2:], ['rename /private/ota-stage/.firmware.456-2.tmp firmware.bin',
                                     'rename /private/ota-stage/.version.456-2.tmp version.json'])

    def test_exact_readback_requires_manifest_to_match_image(self):
        origin = importlib.import_module('verify_authenticated_ota_origin')
        good = {'version':'0.2.9-g1234567', 'sha256':'a'*64, 'size':42}
        origin.verify_expected_identity(good, 'a'*64, 42, good['version'], 'a'*64)
        for changes in ({'sha256':'b'*64}, {'size':41}, {'version':'other'}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                origin.verify_expected_identity(good | changes, 'a'*64, 42,
                                                good['version'], 'a'*64)

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
