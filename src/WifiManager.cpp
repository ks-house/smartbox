#include "WifiManager.h"
#include <Arduino.h>
#include <time.h>

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
            configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
            Serial.println("[TIME] NTP Sync initialized (KST, UTC+9).");
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

void WifiManager::startScan() {
    Serial.println("[WIFI] Starting asynchronous WiFi scan...");
    // Trigger asynchronous scan
    WiFi.scanNetworks(true, false, false, 150);
}

int WifiManager::getScanStatus() {
    return WiFi.scanComplete();
}

String WifiManager::getScanResultsJson() {
    int n = WiFi.scanComplete();
    if (n < 0) {
        return "[]";
    }
    
    String json = "[";
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i));
        json += "}";
    }
    json += "]";
    
    // Free memory allocated for scan results
    WiFi.scanDelete();
    return json;
}

void WifiManager::connectTo(const char* ssid, const char* password) {
    _staSsid = ssid;
    _staPass = password ? password : "";
    connected = false;
    lastConnectRetry = millis();
    
    Serial.printf("[WIFI] Initiating dynamic connection to '%s'...\n", _staSsid.c_str());
    WiFi.disconnect();
    WiFi.begin(_staSsid.c_str(), _staPass.c_str());
}

void WifiManager::stopWiFi() {
    connected = false;
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI] Wi-Fi module disabled (WIFI_OFF).");
}

void WifiManager::startWiFi(const char* ssid, const char* password) {
    WiFi.mode(WIFI_STA);
    if (ssid != nullptr && strlen(ssid) > 0) {
        _staSsid = ssid;
        _staPass = password ? password : "";
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
        Serial.printf("[WIFI] Wi-Fi STA mode enabled. Connecting to AP '%s'...\n", _staSsid.c_str());
    } else {
        Serial.println("[WIFI] Wi-Fi STA mode enabled. No credentials provided.");
    }
    lastConnectRetry = millis();
}
