#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#define HTTP_CODE_OK 200
#define HTTPC_DISABLE_FOLLOW_REDIRECTS 0
extern int fixtureQueries;extern String fixtureManifest;
void fixtureManifestHook();
class HTTPClient {public:
 void begin(WiFiClientSecure&,const char*){};void setFollowRedirects(int){};void setAuthorization(const char*,const char*){}
 int GET(){++fixtureQueries;fixtureManifestHook();return 200;}
 String getString(){return fixtureManifest;}
 String errorToString(int){return String("fixture");}void end(){}
};
