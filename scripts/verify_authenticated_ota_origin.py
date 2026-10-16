#!/usr/bin/env python3
"""Main-only external authenticated OTA verification; never print headers or bytes."""
import base64
import hashlib
import json
import os
import re
import ssl
from urllib.error import HTTPError
from urllib.request import build_opener, HTTPSHandler, HTTPRedirectHandler, Request
from ota_auth_contract import validate_urls


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):return None


def request(opener,url,method,authorization=None,limit=8388608):
    headers={"User-Agent":"SGK-authenticated-origin-probe",
             "X-Forwarded-For":"127.0.0.1","X-Real-IP":"127.0.0.1"}
    if authorization:headers["Authorization"]="Basic "+authorization
    try:
        with opener.open(Request(url,method=method,headers=headers),timeout=30) as result:
            data=result.read(limit+1)
            if len(data)>limit:raise ValueError("bounded OTA response exceeded")
            return result.status,data
    except HTTPError as error:
        error.close()
        return error.code,b""


def main():
    version_url,firmware_url=validate_urls(os.environ["SECRET_VERSION_URL"],os.environ["SECRET_FIRMWARE_URL"])
    identity=os.environ["SECRET_OTA_USER"];password=os.environ["SECRET_OTA_PASS"]
    if not identity or not re.fullmatch(r"[a-f0-9]{64}",password):
        raise ValueError("private probe identity missing")
    expected_version=os.environ.get("EXPECTED_VERSION","")
    expected_sha=os.environ.get("EXPECTED_SHA256","")
    if expected_version and not re.fullmatch(r"[A-Za-z0-9.+_-]{1,96}",expected_version):
        raise ValueError("invalid expected version")
    if expected_sha and not re.fullmatch(r"[a-f0-9]{64}",expected_sha):
        raise ValueError("invalid expected image digest")
    opener=build_opener(NoRedirect(),HTTPSHandler(context=ssl.create_default_context()))
    valid=base64.b64encode((identity+":"+password).encode()).decode()
    wrong=base64.b64encode((identity+":invalid-probe-password").encode()).decode()
    denied=accepted=0
    bodies={}
    for url in (version_url,firmware_url):
        for method in ("HEAD","GET"):
            for header in (None,wrong):
                code,_=request(opener,url,method,header)
                if code not in (401,403):raise RuntimeError("outside anonymous/wrong-password access was not denied")
                denied+=1
            code,data=request(opener,url,method,valid)
            if code!=200:raise RuntimeError("outside authenticated OTA access failed")
            accepted+=1
            if method=="GET":bodies[url]=data
    manifest=json.loads(bodies[version_url])
    version=manifest.get("version","")
    if not re.fullmatch(r"[A-Za-z0-9.+_-]{1,96}",version):
        raise ValueError("bounded manifest version missing")
    digest=hashlib.sha256(bodies[firmware_url]).hexdigest()
    if expected_version and version!=expected_version:raise RuntimeError("outside version mismatch")
    if expected_sha and digest!=expected_sha:raise RuntimeError("outside image mismatch")
    print(json.dumps({"outside_denial_cases":denied,"outside_authenticated_cases":accepted,
                      "tls_hostname_chain_verified":True,"version":version,
                      "firmware_sha256":digest,"firmware_bytes":len(bodies[firmware_url])},
                     sort_keys=True))


if __name__=="__main__":main()
