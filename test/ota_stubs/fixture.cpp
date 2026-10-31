#include "Arduino.h"
#include "HTTPUpdate.h"
#include "MockHardware.h"
#include "SmartBoxController.h"
#include "AutoOtaManager.h"
#include "WifiManager.h"
#include "PowerManager.h"
#include <iostream>
MockSerial Serial;ESPFixture ESP;HTTPUpdateFixture httpUpdate;
int fixtureQueries=0,fixtureFlashes=0,reboots=0,hourNow=12,dayNow=100,timeCalls=0;bool rebootGuardLeaked=false;
String fixtureManifest("{\"version\":\"9.9.9-gabcdef0\"}");
MockHardware* hardware=nullptr;SmartBoxController* controller=nullptr;std::string scenario;
unsigned long millis(){return hardware->getMillis();}
bool getLocalTime(struct tm* t,uint32_t){++timeCalls;std::memset(t,0,sizeof(*t));t->tm_year=126;t->tm_yday=dayNow;t->tm_hour=hourNow;return true;}
bool WifiManager::isConnected(){return true;}
void ESPFixture::restart(){++reboots;}
void faultPower(){hardware->setBatteryVoltage(11.0f);for(int i=0;i<17;i++){hardware->advanceMillis(200);controller->servicePowerDuringOta();}}
void delay(unsigned long ms){hardware->advanceMillis(ms);if(ms==500 && scenario=="contact_fault")faultPower();}
void fixtureManifestHook(){if(scenario=="reboot_guard"){int before=timeCalls;PowerManager::update();rebootGuardLeaked=timeCalls!=before;}if(scenario=="busy_manifest"){controller->forceOpen();controller->update();}}
void fixtureBeforeFlashHook(){if(scenario=="last_admission_fault")faultPower();}
void fixtureFlashHook(){if(scenario=="no_update_fault")faultPower();if(scenario=="close_flash")controller->forceClose();}
void stable(float v,int seconds){hardware->setBatteryVoltage(v);for(int i=0;i<seconds;i++){hardware->advanceMillis(1000);controller->update();}}
int main(int argc,char** argv){
 scenario=argc>1?argv[1]:"";MockHardware hw;hardware=&hw;hw.setBatteryVoltage(12.6f);SmartBoxController box(hw);controller=&box;box.init();stable(12.6f,35);AutoOtaManager::init(box);PowerManager::init(box);
 bool ok=true;
 if(scenario=="catchup"){
   fixtureManifest=String("{\"version\":\"")+box.getFirmwareVersion()+"\"}";
   AutoOtaManager::update();int first=fixtureQueries;
   ++dayNow;hourNow=0;stable(11.8f,35);AutoOtaManager::update();
   hourNow=12;stable(12.6f,35);AutoOtaManager::update();
   ok=first==1 && fixtureQueries==2;
   stable(12.6f,35);AutoOtaManager::update();ok=ok && fixtureQueries==2;
 }else{
   ok=AutoOtaManager::startOtaUpdate(true);AutoOtaManager::update();
   if(scenario=="no_update_fault"){box.update();ok=ok && fixtureFlashes==1 && box.getCurrentState()==STATE_BATTERY_LOW_SHUTDOWN && hw.getPinMode(6)==INPUT;}
   if(scenario=="contact_fault" || scenario=="last_admission_fault")ok=ok && fixtureFlashes==0 && hw.getPinMode(6)==INPUT;
   if(scenario=="close_flash")ok=ok && fixtureFlashes==1 && hw.getPinMode(6)==INPUT;
   if(scenario=="reboot_guard")ok=ok && !rebootGuardLeaked;
   if(scenario=="busy_manifest")ok=ok && fixtureFlashes==0 && box.getCurrentState()==STATE_OPENING;
 }
 if(!ok || reboots){std::cerr<<"fixture assertion failed: "<<scenario<<" queries="<<fixtureQueries<<" flashes="<<fixtureFlashes<<" state="<<box.getCurrentState()<<" main="<<int(hw.getPinMode(6))<<"\n";return 1;}
 return 0;
}
