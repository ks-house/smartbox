#include <thread>
#include <vector>
#include <atomic>
#include "Arduino.h"
#include "HTTPUpdate.h"
#include "MockHardware.h"
#include "SmartBoxController.h"
#include "AutoOtaManager.h"
#include "WifiManager.h"
#include "PowerManager.h"
#include <iostream>
MockSerial Serial;ESPFixture ESP;HTTPUpdateFixture httpUpdate;
int fixtureHttpCode=200;
int fixtureQueries=0,fixtureFlashes=0,reboots=0,hourNow=12,dayNow=100,timeCalls=0;bool rebootGuardLeaked=false,cleanupBusyLeaked=false;
String fixtureManifest("{\"version\":\"9.9.9-gabcdef0\"}");
MockHardware* hardware=nullptr;SmartBoxController* controller=nullptr;std::string scenario;
unsigned long millis(){return hardware->getMillis();}
bool getLocalTime(struct tm* t,uint32_t){++timeCalls;std::memset(t,0,sizeof(*t));t->tm_year=126;t->tm_yday=dayNow;t->tm_hour=hourNow;return true;}
bool WifiManager::isConnected(){return true;}
void ESPFixture::restart(){++reboots;}
void faultPower(){hardware->setBatteryVoltage(11.0f);for(int i=0;i<17;i++){hardware->advanceMillis(200);controller->servicePowerDuringOta();}}
void delay(unsigned long ms){hardware->advanceMillis(ms);if(ms==500 && (scenario=="contact_fault" || scenario=="recovery_known_low"))faultPower();}
void fixtureManifestHook(){if(scenario=="recovery_expiry_after_manifest"){hardware->advanceMillis(1500);controller->servicePowerDuringOta();}if(scenario=="reboot_guard"){int before=timeCalls;PowerManager::update();rebootGuardLeaked=timeCalls!=before;}if(scenario=="busy_manifest"){controller->forceOpen();controller->update();}}
void fixtureBeforeFlashHook(){if(scenario=="recovery_expiry_final"){hardware->advanceMillis(600);controller->servicePowerDuringOta();}if(scenario=="last_admission_fault")faultPower();}
int fixtureFlashResult(){return scenario=="recovery_fail" ? -1 : 0;}
void fixtureFlashHook(){if(scenario=="no_update_fault")faultPower();if(scenario=="close_flash")controller->forceClose();}
void stable(float v,int seconds){hardware->setBatteryVoltage(v);for(int i=0;i<seconds;i++){hardware->advanceMillis(1000);controller->update();}}
int main(int argc,char** argv){
 scenario=argc>1?argv[1]:"";MockHardware hw;hardware=&hw;hw.setBatteryVoltage(12.6f);SmartBoxController box(hw);controller=&box;box.init();stable(12.6f,35);AutoOtaManager::init(box);PowerManager::init(box);
 bool ok=true;
 if(scenario.rfind("recovery_",0)==0 || scenario=="normal_sensor_fault"){
   if(scenario=="recovery_preempt_normal")ok=AutoOtaManager::startOtaUpdate(true);
   stable(NAN,125);
   box.registerStateCallback([](State previous,State next){if(previous==STATE_OTA_UPDATING && next==STATE_BATTERY_LOW_SHUTDOWN && !AutoOtaManager::isOtaInProgress())cleanupBusyLeaked=true;});
   if(scenario=="recovery_replace_expired"){ok=AutoOtaManager::startOtaUpdate(true,true,1000);hw.advanceMillis(1500);box.update();}
   if(scenario=="recovery_current")fixtureManifest=String("{\"version\":\"")+box.getFirmwareVersion()+"\"}";
   if(scenario=="recovery_manifest_fail")fixtureHttpCode=503;
   if(scenario=="normal_sensor_fault")ok=!AutoOtaManager::startOtaUpdate(true) && fixtureQueries==0;
   else if(scenario=="recovery_concurrent"){std::atomic<int> accepted{0};std::vector<std::thread> senders;for(int i=0;i<8;++i)senders.emplace_back([&]{if(AutoOtaManager::startOtaUpdate(true,true))++accepted;});for(auto& sender:senders)sender.join();ok=accepted==1;AutoOtaManager::update();}
   else {ok=AutoOtaManager::startOtaUpdate(true,true,scenario.find("expiry")!=std::string::npos?1000:120000);if(scenario=="recovery_expiry_queued"){hw.advanceMillis(1500);box.update();}AutoOtaManager::update();}
   const bool shouldFlash=scenario=="recovery_no_update" || scenario=="recovery_fail" || scenario=="recovery_preempt_normal" || scenario=="recovery_replace_expired" || scenario=="recovery_concurrent";
   ok=ok && fixtureFlashes==(shouldFlash?1:0) && box.getCurrentState()==STATE_BATTERY_LOW_SHUTDOWN && !AutoOtaManager::isOtaInProgress();
   for(int pin:{6,7,8})ok=ok && hw.getPinMode(pin)==INPUT;
   if(scenario!="normal_sensor_fault" && scenario!="recovery_expiry_queued")ok=ok && !AutoOtaManager::startOtaUpdate(true,true);
 }else if(scenario=="catchup"){
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
 if(!ok || reboots || cleanupBusyLeaked){std::cerr<<"fixture assertion failed: "<<scenario<<" queries="<<fixtureQueries<<" flashes="<<fixtureFlashes<<" state="<<box.getCurrentState()<<" main="<<int(hw.getPinMode(6))<<"\n";return 1;}
 return 0;
}
