"""Sanitized exact CI binary identity; never publish the credential-bearing image as an Actions artifact."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import re

def metadata(path,version,commit,build_number):
    match=re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+-g([0-9a-f]{7,40})',version)
    if not match or not re.fullmatch(r'[a-f0-9]{40}',commit) or not commit.startswith(match.group(1)) or not re.fullmatch(r'[1-9][0-9]*',build_number):
        raise ValueError('clean matching CI version/commit/build identity required')
    raw=path.read_bytes()
    if not raw:raise ValueError('empty firmware is not publishable')
    return {'version':version,'build_number':build_number,'commit':commit,
            'date':datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
            'sha256':hashlib.sha256(raw).hexdigest(),'size':len(raw),'power_schema':1}

def main():
    directory=Path('.pio/build/esp32-c6-devkitc-1')
    value=metadata(directory/'firmware.bin',os.environ['FULL_VERSION'],os.environ['GITHUB_SHA'],os.environ['GITHUB_RUN_ID'])
    (directory/'version.json').write_text(json.dumps(value,separators=(',',':'))+'\n',encoding='utf-8')
    print(json.dumps(value))
if __name__=='__main__':main()
