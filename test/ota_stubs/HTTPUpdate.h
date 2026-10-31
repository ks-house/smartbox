#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
enum t_httpUpdate_return {HTTP_UPDATE_FAILED=-1,HTTP_UPDATE_NO_UPDATES=0,HTTP_UPDATE_OK=1};
extern int fixtureFlashes;void fixtureFlashHook();void fixtureBeforeFlashHook();
class HTTPUpdateFixture {public:
 void setFollowRedirects(int){};void setAuthorization(const char*,const char*){fixtureBeforeFlashHook();}
 t_httpUpdate_return update(WiFiClientSecure&,const char*){++fixtureFlashes;fixtureFlashHook();return HTTP_UPDATE_NO_UPDATES;}
 int getLastError(){return 0;}String getLastErrorString(){return String("fixture");}
};extern HTTPUpdateFixture httpUpdate;
