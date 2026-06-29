#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

enum class ProvState {
    IDLE,
    CONNECTING,
    SUCCESS,
    FAILED
};

class WifiManager {
private:
    static unsigned long lastConnectRetry;
    static bool connected;
    static DNSServer dnsServer;
    static WebServer webServer;
    static String _apSsid;
    static String _apPass;
    static unsigned long bootTime;
    static bool apTimeoutReached;

    static ProvState provState;
    static String stationIp;
    static unsigned long connectStartTime;

    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    static void handleRoot();
    static void handleSave();
    static void handleStatus();

public:
    static void init(const char* apSsid, const char* apPass, const char* staSsid = nullptr, const char* staPass = nullptr);
    static void update();
    static bool isConnected();
    static void startScan();
    static int getScanStatus();
    static String getScanResultsJson();
    static void connectTo(const char* ssid, const char* password);
    static void stopWiFi();
    static void startWiFi(const char* ssid, const char* password);
};

#endif // WIFI_MANAGER_H
