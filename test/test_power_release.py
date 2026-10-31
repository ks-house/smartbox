import importlib.util,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('release',ROOT/'scripts/write_release_metadata.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class ReleaseTests(unittest.TestCase):
    def test_exact_binary_size_and_digest(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/'firmware.bin';p.write_bytes(b'abc')
            meta=m.metadata(p,'0.2.0-gabcdef0','abcdef0'+'a'*33,'123')
            self.assertEqual('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',meta['sha256']);self.assertEqual(3,meta['size'])
            p.write_bytes(b'abd');self.assertNotEqual(meta['sha256'],m.metadata(p,'0.2.0-gabcdef0','abcdef0'+'a'*33,'123')['sha256'])
    def test_dirty_or_malformed_identity_is_not_publishable(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/'firmware.bin';p.write_bytes(b'abc')
            for v in ['0.2.0-gabcdef0-dirty','bad','0.2.0-gabcdef0\n']:
                with self.assertRaises(ValueError):m.metadata(p,v,'abcdef0'+'a'*33,'123')
    def test_version_must_match_full_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/'firmware.bin';p.write_bytes(b'abc')
            with self.assertRaises(ValueError):m.metadata(p,'0.2.0-gabcdef0','f'*40,'123')
    def test_empty_binary_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/'firmware.bin';p.write_bytes(b'')
            with self.assertRaises(ValueError):m.metadata(p,'0.2.0-gabcdef0','abcdef0'+'a'*33,'123')
if __name__=='__main__':unittest.main()
