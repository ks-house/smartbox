#include "WifiManager.h"
#include <Arduino.h>

unsigned long WifiManager::lastConnectRetry = 0;
bool WifiManager::connected = false;
static String _staSsid = "";
static String _staPass = "";

void WifiManager::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch(event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("[WIFI] Connected to external WiFi AP successfully.");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("[WIFI] Station got IP address: ");
            Serial.println(WiFi.localIP());
            connected = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (connected) {
                Serial.println("[WIFI] Disconnected from external WiFi AP.");
                connected = false;
            }
            break;
        default:
            break;
    }
}

void WifiManager::init(const char* apSsid, const char* apPass, const char* staSsid, const char* staPass) {
    WiFi.onEvent(onWiFiEvent);
    
    // Set both Access Point and Station modes
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid, apPass);
    Serial.printf("[WIFI] SoftAP '%s' active. IP: %s\n", apSsid, WiFi.softAPIP().toString().c_str());
    
    if (staSsid != nullptr && strlen(staSsid) > 0) {
        _staSsid = staSsid;
        _staPass = staPass ? staPass : "";
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
        Serial.printf("[WIFI] Station connecting to AP '%s'...\n", _staSsid.c_str());
        lastConnectRetry = millis();
    }
}

void WifiManager::update() {
    // Non-blocking auto-reconnect polling every 15 seconds
    if (!_staSsid.isEmpty() && !connected && (millis() - lastConnectRetry >= 15000)) {
        lastConnectRetry = millis();
        Serial.printf("[WIFI] Attempting connection retry to AP '%s'...\n", _staSsid.c_str());
        WiFi.disconnect();
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
    }
}

bool WifiManager::isConnected() {
    return connected;
}
