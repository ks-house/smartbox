"""Exercise the real Preferences loader; no ESP32 or credential access."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
ARDUINO = """#pragma once
#include <string>
using String=std::string;
"""
PREFERENCES = r"""
#pragma once
#include <map>
#include <string>
#include <cmath>
#include <limits>
class Preferences {
 public:
  inline static std::map<std::string,float> floats;
  inline static std::map<std::string,unsigned long> ulongs;
  inline static std::map<std::string,int> ints;
  inline static int writes=0;
  inline static bool writeFailure=false,openFailure=false;
  bool readOnly=true;
  bool begin(const char*,bool ro){readOnly=ro;return !openFailure;}
  void end(){}
  float getFloat(const char* k,float d){return floats.count(k)?floats[k]:d;}
  unsigned long getULong(const char* k,unsigned long d){return ulongs.count(k)?ulongs[k]:d;}
  int getInt(const char* k,int d){return ints.count(k)?ints[k]:d;}
  bool getBool(const char*,bool d){return d;}
  String getString(const char*,const char* d){return d;}
  size_t putFloat(const char* k,float v){if(readOnly||writeFailure)return 0;floats[k]=v;++writes;return sizeof(float);}
  size_t putULong(const char*,unsigned long){++writes;return sizeof(unsigned long);}
  size_t putInt(const char*,int){++writes;return sizeof(int);}
  size_t putBool(const char*,bool){++writes;return sizeof(bool);}
  size_t putString(const char*,const String&){++writes;return 1;}
};
"""
FIXTURE=r"""
#include <cassert>
#include <cstdlib>
#include <limits>
#include "ConfigManager.h"
#include "Preferences.h"
MockSerial Serial;
int main(int argc,char**argv) {
 assert(argc==2);int scenario=std::atoi(argv[1]);
 Preferences::floats["curr_lim"]=5000;
 Preferences::ulongs["act_time"]=3800;
 Preferences::ints["ota_hour"]=2;
 if(scenario==0||scenario==4)Preferences::floats["volt_lim"]=11.3f;
 else if(scenario==1)Preferences::floats["volt_lim"]=11.0f;
 else if(scenario==2)Preferences::floats["volt_lim"]=11.8f;
 else if(scenario==3)Preferences::floats["volt_lim"]=std::numeric_limits<float>::quiet_NaN();
 if(scenario==4)Preferences::writeFailure=true;
 if(scenario==5)Preferences::openFailure=true;
 BoxConfig config;ConfigManager::loadConfig(config);
 assert(config.voltageShutdownLimit==(scenario==2?11.8f:11.0f));
 if(scenario!=5){
  assert(config.currentStallLimit==5000);
  assert(config.actuatorTime==3800);
  assert(config.otaHour==2);
 }
 if(scenario==0||scenario==3){
  assert(Preferences::floats["volt_lim"]==11.0f);
  assert(Preferences::writes==1);
  ConfigManager::loadConfig(config);assert(Preferences::writes==1);
 }else assert(Preferences::writes==0);
 return 0;
}
"""
class PersistedThresholdTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();p=pathlib.Path(cls.tmp.name);cls.binary=p/'config'
  (p/'Arduino.h').write_text(ARDUINO);(p/'Preferences.h').write_text(PREFERENCES);(p/'fixture.cpp').write_text(FIXTURE)
  cmd=['g++','-std=c++17','-DNATIVE_BUILD','-I'+str(p),'-I'+str(ROOT/'src'),'-include',str(p/'Arduino.h'),str(ROOT/'src/ConfigManager.cpp'),str(p/'fixture.cpp'),'-o',str(cls.binary)]
  result=subprocess.run(cmd,capture_output=True,text=True);assert result.returncode==0,result.stderr
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def scenario(self,n):
  result=subprocess.run([str(self.binary),str(n)],capture_output=True,text=True)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_old_11_3_saved_value_migrates_only_cutoff_once(self):self.scenario(0)
 def test_new_11_0_value_does_not_write_each_boot(self):self.scenario(1)
 def test_custom_cutoff_is_preserved(self):self.scenario(2)
 def test_invalid_saved_value_uses_new_default(self):self.scenario(3)
 def test_failed_persistence_still_uses_new_runtime_value(self):self.scenario(4)
 def test_missing_preferences_uses_new_default_without_writes(self):self.scenario(5)
if __name__=='__main__':unittest.main()
