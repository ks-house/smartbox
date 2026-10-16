import importlib.util
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from ota_auth_contract import validate_urls
from verify_authenticated_ota_origin import NoRedirect


class OtaContractTest(unittest.TestCase):
    def test_same_https_origin_with_exact_paths(self):
        pair=("https://nas.example:4443/version.json","https://nas.example:4443/firmware.bin")
        self.assertEqual(pair,validate_urls(*pair))
    def test_foreign_origin_cleartext_userinfo_queries_or_other_files_rejected(self):
        good="https://nas.example:4443/firmware.bin"
        cases=["http://nas.example:4443/version.json",
               "https://other.example:4443/version.json",
               "https://nas.example:443/version.json",
               "https://user:secret@nas.example:4443/version.json",
               "https://nas.example:4443/version.json?token=secret",
               "https://nas.example:4443/other.json"]
        for value in cases:
            with self.subTest(value=value),self.assertRaises(ValueError):validate_urls(value,good)
    def test_authentication_probe_never_follows_redirects(self):
        self.assertIsNone(NoRedirect().redirect_request(None,None,302,"redirect",{},"https://other.example/firmware.bin"))
