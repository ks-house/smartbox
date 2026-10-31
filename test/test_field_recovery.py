import importlib.util,json,pathlib,subprocess,tempfile,unittest,zipfile,struct
ROOT=pathlib.Path(__file__).resolve().parents[1]
class FieldBundleTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.directory=pathlib.Path(cls.tmp.name)
  p=subprocess.run(['openssl','req','-x509','-newkey','rsa:3072','-nodes','-keyout',str(cls.directory/'key.pem'),'-out',str(cls.directory/'cert.pem'),'-days','1','-subj','/CN=field-kit-test'],capture_output=True)
  assert p.returncode==0
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def load(self):
  path=ROOT/'scripts/build_field_recovery.py'
  if not path.exists():self.fail('private field bundle builder is missing')
  spec=importlib.util.spec_from_file_location('field_bundle',path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
 def environment(self):
  return dict(SECRET_WIFI_SSID='test-ssid',SECRET_WIFI_PASS='test-wifi-pass',SECRET_AP_PASS='test-ap-pass',SECRET_VERSION_URL='https://nas.test/version.json',SECRET_FIRMWARE_URL='https://nas.test/firmware.bin',SECRET_OTA_USER='test_ota',SECRET_OTA_PASS='test_ota_password_123456',SECRET_MQTT_HOST='mqtt.test',SECRET_MQTT_PORT='4883',SECRET_MQTT_USER='test_smartbox',SECRET_MQTT_PASS='test_mqtt_password_123456',SECRET_ROOT_CA_CERT=(self.directory/'cert.pem').read_text())
 def test_missing_or_placeholder_credentials_never_build(self):
  m=self.load()
  for key in ['SECRET_WIFI_PASS','SECRET_OTA_PASS','SECRET_MQTT_USER','SECRET_ROOT_CA_CERT']:
   env=self.environment();env[key]='';self.assertRaises(ValueError,m.secret_header,env)
  env=self.environment();env['SECRET_MQTT_USER']='your_mqtt_user';self.assertRaises(ValueError,m.secret_header,env)
 def test_secret_quoting_is_c_literal_not_source_injection(self):
  m=self.load();env=self.environment();env['SECRET_WIFI_SSID']='test"\\network';text=m.secret_header(env)
  self.assertIn('#define SECRET_WIFI_SSID "test\\"\\\\network"',text);self.assertNotIn('YOUR_',text)
 def test_recovery_layout_requires_same_16mb_two_slots(self):
  m=self.load();raw=b''.join(struct.pack('<HBBII16sI',0x50aa,t,s,o,n,name.encode().ljust(16,b'\0'),0) for t,s,o,n,name in [(1,2,0x9000,0x5000,'nvs'),(1,0,0xe000,0x2000,'otadata'),(0,0x10,0x10000,0x640000,'app0'),(0,0x11,0x650000,0x640000,'app1')])
  self.assertEqual(0x10000,m.validate_layout(raw)['app0'])
  self.assertRaises(ValueError,m.validate_layout,raw.replace(struct.pack('<I',0x640000),struct.pack('<I',0x140000),1))
 def test_encrypted_artifact_roundtrip_has_no_plain_firmware(self):
  m=self.load();src=self.directory/'inner.zip';src.write_bytes(b'PK-test-private-image-and-credentials');out=self.directory/'test.cms'
  m.encrypt_bundle(src,self.directory/'cert.pem',out)
  self.assertNotIn(b'credentials',out.read_bytes());self.assertNotEqual(src.read_bytes(),out.read_bytes())
  p=subprocess.run(['openssl','cms','-decrypt','-binary','-inform','DER','-in',str(out),'-recip',str(self.directory/'cert.pem'),'-inkey',str(self.directory/'key.pem')],capture_output=True)
  self.assertEqual(0,p.returncode);self.assertEqual(src.read_bytes(),p.stdout)
  tampered=bytearray(out.read_bytes());tampered[-20]^=1;(self.directory/'tampered.cms').write_bytes(tampered)
  p=subprocess.run(['openssl','cms','-decrypt','-binary','-inform','DER','-in',str(self.directory/'tampered.cms'),'-recip',str(self.directory/'cert.pem'),'-inkey',str(self.directory/'key.pem')],capture_output=True)
  self.assertNotEqual(0,p.returncode)
 def test_recipient_input_cannot_contain_a_private_key(self):
  m=self.load();self.assertRaises(ValueError,m.validate_recipient,(self.directory/'key.pem').read_bytes())
 def test_wrong_chip_or_dirty_identity_rejected(self):
  m=self.load();raw=bytearray(256);raw[0]=0xe9;raw[12]=9
  self.assertRaises(ValueError,m.validate_image,raw,'0.2.1-g'+'a'*7,'a'*40)
  raw[12]=13;raw+=b'0.2.1-gaaaaaaa-dirty'
  self.assertRaises(ValueError,m.validate_image,raw,'0.2.1-gaaaaaaa-dirty','a'*40)

 def test_valid_header_compiles_and_embedded_nul_is_rejected(self):
  m=self.load();env=self.environment();p=subprocess.run(['g++','-x','c++','-fsyntax-only','-'],input=m.secret_header(env)+'\nconst char *v=SECRET_ROOT_CA_CERT;\n',capture_output=True,text=True)
  self.assertEqual(0,p.returncode,p.stderr)
  env['SECRET_WIFI_SSID']='bad'+chr(0)+'ssid';self.assertRaises(ValueError,m.secret_header,env)
 def test_correct_image_and_identity_are_accepted(self):
  m=self.load();version='0.2.1-gaaaaaaa';raw=bytearray(512);raw[0]=0xe9;raw[3]=64;raw[12]=13;raw[32:32+len(version)+1]=version.encode()+bytes([0]);m.validate_image(raw,version,'a'*40)


 def test_eight_mb_header_cannot_be_packaged_as_16mb(self):
  m=self.load();version='0.2.1-gaaaaaaa';raw=bytearray(512);raw[0]=233;raw[3]=48;raw[12]=13;raw[32:32+len(version)+1]=version.encode()+bytes([0]);self.assertRaises(ValueError,m.validate_image,raw,version,'a'*40)

if __name__=='__main__':unittest.main()
