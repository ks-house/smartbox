#!/usr/bin/env python3
"""Refuse publication if an external runner can fetch credential-bearing firmware."""
import json
import os
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file, code, message, headers, newurl):
        return None


def verify(url, opener=None):
    parsed = urllib.parse.urlsplit(url)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username is not None
            or parsed.password is not None or parsed.query or parsed.fragment
            or parsed.path != "/firmware.bin"):
        raise ValueError("expected credential-free HTTPS firmware URL without redirects")
    if opener is None:
        opener = urllib.request.build_opener(NoRedirect(),
                    urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    # Both methods are required; HEAD-only refusal can hide an open GET route.
    for method in ("HEAD", "GET"):
        request = urllib.request.Request(url, method=method,
                    headers={"Range": "bytes=0-0", "X-Forwarded-For": "127.0.0.1",
                             "X-Real-IP": "127.0.0.1"})
        try:
            with opener.open(request, timeout=15) as response:
                code = response.status
        except urllib.error.HTTPError as error:
            code = error.code
            error.close()
        if code not in (401, 403):
            raise ValueError("public credential-bearing firmware access is not denied")
    return {"public_firmware_access_denied": True, "methods": ["HEAD", "GET"],
            "device_local_download_verified": False}


def main():
    try:
        result = verify(os.environ.get("SECRET_FIRMWARE_URL", ""))
    except Exception:
        # Never print a secret URL, redirects, request object or HTTP error body.
        print("OTA origin protection verification failed", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
