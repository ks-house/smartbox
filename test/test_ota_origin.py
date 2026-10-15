"""Gate behavior with real urllib request objects; no network/production secret."""
import io
import unittest
import urllib.error
from scripts.verify_private_ota_origin import verify


class Opener:
    def __init__(self, statuses):
        self.statuses = iter(statuses)
        self.requests = []

    def open(self, request, timeout):
        self.requests.append(request)
        code = next(self.statuses)
        raise urllib.error.HTTPError("https://example.test/firmware.bin",
                                     code, "fixture", {}, io.BytesIO(b""))


class OriginGateTest(unittest.TestCase):
    def test_head_denial_does_not_hide_open_get(self):
        with self.assertRaises(ValueError):
            verify("https://example.test/firmware.bin", Opener([403, 200]))

    def test_both_methods_require_denial_and_test_spoofed_private_headers(self):
        opener = Opener([403, 403])
        self.assertTrue(verify("https://example.test/firmware.bin", opener)
                        ["public_firmware_access_denied"])
        self.assertEqual(["HEAD", "GET"], [r.method for r in opener.requests])
        self.assertTrue(all(r.get_header("X-forwarded-for") == "127.0.0.1"
                            for r in opener.requests))

    def test_redirects_and_ambiguous_errors_never_pass(self):
        for status in [200, 206, 301, 302, 404, 500, 503]:
            with self.subTest(status=status), self.assertRaises(ValueError):
                verify("https://example.test/firmware.bin", Opener([status]))

    def test_urls_cannot_embed_credentials_or_switch_path(self):
        for url in ["http://example.test/firmware.bin",
                    "https://user:secret@example.test/firmware.bin",
                    "https://example.test/other.bin",
                    "https://example.test/firmware.bin?key=secret",
                    "https://example.test/firmware.bin#fragment"]:
            with self.subTest(url=url), self.assertRaises(ValueError):
                verify(url, Opener([403, 403]))


if __name__ == "__main__":
    unittest.main()
