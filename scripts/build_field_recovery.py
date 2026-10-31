"""Build exact field-only images; upload authenticated ciphertext, never plaintext firmware."""
from pathlib import Path
import base64,hashlib,json,os,re,struct,subprocess,tempfile,zipfile
NAMES=['WIFI_SSID','WIFI_PASS','AP_PASS','VERSION_URL','FIRMWARE_URL','OTA_USER','OTA_PASS','MQTT_HOST','MQTT_PORT','MQTT_USER','MQTT_PASS','ROOT_CA_CERT']
def secret_header(env):
 values={n:env.get('SECRET_'+n,'') for n in NAMES}
 if any(not v or '\x00' in v or 'YOUR_' in v or v.startswith('your_') for v in values.values()):raise ValueError('complete non-placeholder provisioning required')
 for n in ['OTA_USER','MQTT_USER']:
  if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',values[n]):raise ValueError('invalid principal')
 for n in ['OTA_PASS','MQTT_PASS']:
  if not re.fullmatch(r'[A-Za-z0-9_-]{16,128}',values[n]):raise ValueError('invalid private credential')
 if not 8<=len(values['AP_PASS'].encode())<=63 or not 1<=len(values['WIFI_SSID'].encode())<=32:raise ValueError('invalid Wi-Fi provisioning')
 if not re.fullmatch(r'[0-9]{1,5}',values['MQTT_PORT']) or not 1<=int(values['MQTT_PORT'])<=65535:raise ValueError('invalid MQTT port')
 for n in ['VERSION_URL','FIRMWARE_URL']:
  from urllib.parse import urlsplit
  u=urlsplit(values[n])
  if u.scheme!='https' or not u.hostname or u.username or u.password:raise ValueError('authenticated HTTPS origin required')
 if not re.fullmatch(r'[A-Za-z0-9.-]+',values['MQTT_HOST']):raise ValueError('invalid broker hostname')
 if '-----BEGIN CERTIFICATE-----' not in values['ROOT_CA_CERT'] or 'PRIVATE KEY' in values['ROOT_CA_CERT']:raise ValueError('public TLS trust anchor required')
 return '#ifndef SECRETS_H\n#define SECRETS_H\n'+''.join('#define SECRET_'+n+' '+(values[n] if n=='MQTT_PORT' else json.dumps(values[n]))+'\n' for n in NAMES)+'#endif\n'
def validate_layout(raw):
 entries={}
 for pos in range(0,len(raw)-31,32):
  magic,t,s,offset,size,label,flags=struct.unpack('<HBBII16sI',raw[pos:pos+32])
  if magic==0x50aa:entries[label.split(bytes([0]))[0].decode()]=(t,s,offset,size,flags)
 expected={'nvs':(1,2,0x9000,0x5000,0),'otadata':(1,0,0xe000,0x2000,0),'app0':(0,0x10,0x10000,0x640000,0),'app1':(0,0x11,0x650000,0x640000,0)}
 if any(entries.get(k)!=v for k,v in expected.items()):raise ValueError('reviewed16MB dual-OTA layout required')
 return {'app0':0x10000,'app0_length':0x640000,'app1':0x650000,'app1_length':0x640000,'otadata':0xe000,'otadata_length':0x2000,'nvs':0x9000,'nvs_length':0x5000,'partition_table':0x8000}
def validate_image(raw,version,commit):
 if not re.fullmatch(r'[a-f0-9]{40}',commit) or not re.fullmatch(r'0\.2\.1-g[a-f0-9]{7,40}',version) or not commit.startswith(version.split('-g')[1]):raise ValueError('clean exact correction identity required')
 if not 256<len(raw)<=0x640000 or raw[0]!=0xe9 or raw[3]>>4!=4 or int.from_bytes(raw[12:14],'little')!=13 or (version.encode()+bytes([0])) not in raw:raise ValueError('matching ESP32-C6 application required')
def validate_recipient(raw):
 if b'PRIVATE KEY' in raw or raw.count(b'-----BEGIN CERTIFICATE-----')!=1 or len(raw)>16384:raise ValueError('one public recipient certificate required')
 p=subprocess.run(['openssl','x509','-noout','-checkend','3600'],input=raw,capture_output=True)
 if p.returncode:raise ValueError('recipient certificate invalid or expiring')
def encrypt_bundle(source,certificate,destination):
 validate_recipient(certificate.read_bytes())
 p=subprocess.run(['openssl','cms','-encrypt','-binary','-aes-256-gcm','-outform','DER','-in',str(source),'-out',str(destination),str(certificate)],capture_output=True)
 if p.returncode:destination.unlink(missing_ok=True);raise RuntimeError('authenticated encryption failed; no artifact')
def main():
 root=Path.cwd();commit=os.environ['GITHUB_SHA'];actual=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
 if actual!=commit or commit!=os.environ['FIELD_EXPECTED_SHA'] or subprocess.check_output(['git','status','--porcelain'],text=True).strip():raise ValueError('clean exact requested source required')
 if os.environ.get('GITHUB_EVENT_NAME')!='workflow_dispatch' or os.environ.get('FIELD_OPERATION')!='prepare-field-recovery':raise ValueError('explicit field build dispatch required')
 header=root/'include/secrets.h';generated=root/'src/git_version.h';previous=generated.read_bytes() if generated.exists() else None
 output=Path(os.environ['RUNNER_TEMP'])/'field-encrypted';output.mkdir(mode=0o700);version='0.2.1-g'+commit[:7]
 with tempfile.TemporaryDirectory(dir=os.environ['RUNNER_TEMP']) as temp:
  work=Path(temp);cert=work/'recipient.pem';cert.write_bytes(base64.b64decode(os.environ['FIELD_RECIPIENT_B64'],validate=True));validate_recipient(cert.read_bytes());os.umask(0o077)
  try:
   header.write_text(secret_header(os.environ),encoding='utf-8')
   ca=work/'trust.pem';ca.write_text(os.environ['SECRET_ROOT_CA_CERT'])
   p=subprocess.run(['openssl','x509','-in',str(ca),'-noout','-checkend','86400'],capture_output=True)
   if p.returncode:raise ValueError('TLS trust anchor invalid or expiring')
   p=subprocess.run(['pio','run','-e','esp32-c6-field-recovery','-j','4'],capture_output=True,text=True,timeout=1200)
   if p.returncode:raise RuntimeError('private build failed; no firmware artifact uploaded')
   build=root/'.pio/build/esp32-c6-field-recovery';raw=(build/'firmware.bin').read_bytes();validate_image(raw,version,commit)
   if any(os.environ['SECRET_'+n].encode() not in raw for n in ['OTA_USER','OTA_PASS','MQTT_USER','MQTT_PASS']):raise ValueError('compiled private identities missing')
   layout=validate_layout((build/'partitions.bin').read_bytes())
   boot=(build/'bootloader.bin').read_bytes()
   if boot[0]!=0xe9 or boot[3]>>4!=4 or int.from_bytes(boot[12:14],'little')!=13:raise ValueError('16MB C6 bootloader reference required')
   framework=Path.home()/'.platformio/packages/framework-arduinoespressif32'
   files={'firmware.bin':build/'firmware.bin','partitions.bin':build/'partitions.bin','bootloader.bin':build/'bootloader.bin','boot_app0.bin':framework/'tools/partitions/boot_app0.bin'}
   if files['boot_app0.bin'].stat().st_size!=0x2000:raise ValueError('unexpected OTA selection initializer')
   manifest={'schema':1,'purpose':'field_usb_recovery','production_provisioned':True,'build_environment':'esp32-c6-field-recovery','native_usb_serial':True,'version':version,'commit':commit,'chip':'esp32c6','chip_id':13,'flash_bytes':0x1000000,'layout':layout,'app_write_span':(len(raw)+4095)//4096*4096,'files':{n:{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'size':p.stat().st_size} for n,p in files.items()}}
   archive=work/'private.zip'
   with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for n,p in files.items():z.write(p,n)
    z.writestr('manifest.json',json.dumps(manifest,indent=2))
   encrypted=output/'field-recovery.cms';encrypt_bundle(archive,cert,encrypted)
   public={'commit':commit,'version':version,'firmware_sha256':manifest['files']['firmware.bin']['sha256'],'encrypted_sha256':hashlib.sha256(encrypted.read_bytes()).hexdigest(),'encryption':'CMS AuthEnvelopedData AES-256-GCM','run_id':os.environ['GITHUB_RUN_ID']}
   (output/'build-public.json').write_text(json.dumps(public,indent=2));print(json.dumps(public))
  finally:
   header.unlink(missing_ok=True)
   if previous is not None:generated.write_bytes(previous)
   else:generated.unlink(missing_ok=True)
if __name__=='__main__':main()
