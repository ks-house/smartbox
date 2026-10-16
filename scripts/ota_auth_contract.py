#!/usr/bin/env python3
"""Validate one HTTPS origin without credentials in URLs."""
import os
import re
from urllib.parse import urlsplit


def validate_urls(version_url, firmware_url):
    parsed=[]
    for value,path in ((version_url,"/version.json"),(firmware_url,"/firmware.bin")):
        try:
            item=urlsplit(value);port=item.port or 443
        except ValueError:
            raise ValueError("invalid OTA URL structure") from None
        if (item.scheme!="https" or not item.hostname or item.username or item.password
                or item.path!=path or item.query or item.fragment):
            raise ValueError("OTA requires fixed HTTPS paths without URL credentials")
        parsed.append((item.hostname.lower(),port))
    if parsed[0]!=parsed[1]:
        raise ValueError("OTA URLs must share one verified TLS origin")
    return version_url,firmware_url


def main():
    validate_urls(os.environ["SECRET_VERSION_URL"],os.environ["SECRET_FIRMWARE_URL"])
    user=os.environ["SECRET_OTA_USER"];password=os.environ["SECRET_OTA_PASS"]
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}",user) or user=="your_ota_user":
        raise ValueError("production OTA principal required")
    if not re.fullmatch(r"[a-f0-9]{64}",password):
        raise ValueError("production OTA random credential required")
    print("HTTPS OTA provisioning contract valid")


if __name__=="__main__":main()
