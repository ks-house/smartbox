#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>

class WifiManager {
private:
    static unsigned long lastConnectRetry;
    static bool connected;
    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

public:
    static void init(const char* apSsid, const char* apPass, const char* staSsid = nullptr, const char* staPass = nullptr);
    static void update();
    static bool isConnected();
};

#endif // WIFI_MANAGER_H
