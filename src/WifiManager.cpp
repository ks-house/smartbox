#include "WifiManager.h"
#include "ConfigManager.h"
#include <Arduino.h>
#include <time.h>

unsigned long WifiManager::lastConnectRetry = 0;
bool WifiManager::connected = false;
DNSServer WifiManager::dnsServer;
WebServer WifiManager::webServer(80);
String WifiManager::_apSsid = "";
String WifiManager::_apPass = "";
unsigned long WifiManager::bootTime = 0;
bool WifiManager::apTimeoutReached = false;

static String _staSsid = "";
static String _staPass = "";

namespace {
    String escapeJson(const String& input) {
        String out;
        out.reserve(input.length() + 8);
        for (unsigned int i = 0; i < input.length(); i++) {
            char c = input.charAt(i);
            if      (c == '"')  { out += '\\'; out += '"'; }
            else if (c == '\\') { out += '\\'; out += '\\'; }
            else if (c == '\n') { out += '\\'; out += 'n'; }
            else if (c == '\r') { out += '\\'; out += 'r'; }
            else if (c == '\t') { out += '\\'; out += 't'; }
            else                { out += c; }
        }
        return out;
    }
}

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

void WifiManager::handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>SmartBox Wi-Fi Setup</title><style>"
                  "body{font-family:sans-serif;background:#121212;color:#eee;padding:20px;text-align:center}"
                  ".card{background:#1e1e1e;max-width:400px;margin:0 auto;padding:20px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.5)}"
                  "input{width:100%;padding:10px;margin:8px 0;box-sizing:border-box;border-radius:6px;border:1px solid #444;background:#2a2a2a;color:#fff}"
                  "button{width:100%;padding:12px;background:#00e676;border:none;border-radius:6px;color:#000;font-weight:bold;cursor:pointer;margin-top:10px}"
                  "</style></head><body><div class='card'>"
                  "<h2>📶 SmartBox Wi-Fi Setup</h2>"
                  "<p>Enter your local Wi-Fi router credentials below. (AP active for 5 min on boot)</p>"
                  "<form action='/save' method='POST'>"
                  "<input type='text' name='ssid' placeholder='Wi-Fi Network Name (SSID)' required><br>"
                  "<input type='password' name='pass' placeholder='Password'><br>"
                  "<button type='submit'>Save & Connect</button>"
                  "</form></div></body></html>";
    webServer.send(200, "text/html", html);
}

void WifiManager::handleSave() {
    if (webServer.hasArg("ssid")) {
        String ssid = webServer.arg("ssid");
        String pass = webServer.arg("pass");
        
        Serial.printf("[WIFI] Provisioning new credentials received: SSID='%s'\n", ssid.c_str());
        ConfigManager::saveWifiCredentials(ssid.c_str(), pass.c_str());
        
        String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>Saved</title><style>body{font-family:sans-serif;background:#121212;color:#eee;text-align:center;padding:50px;}</style></head>"
                      "<body><h2>✅ Wi-Fi Credentials Saved!</h2><p>Connecting to <b>" + ssid + "</b>...</p></body></html>";
        webServer.send(200, "text/html", html);
        delay(1000);
        connectTo(ssid.c_str(), pass.c_str());
    } else {
        webServer.send(400, "text/plain", "Bad Request");
    }
}

void WifiManager::init(const char* apSsid, const char* apPass, const char* staSsid, const char* staPass) {
    WiFi.onEvent(onWiFiEvent);
    bootTime = millis();
    apTimeoutReached = false;
    
    _apSsid = apSsid ? apSsid : "";
    static const char* DEFAULT_AP_PASS = "smartbox_admin";
    const char* effectiveApPass = apPass;
    if (effectiveApPass == nullptr || strlen(effectiveApPass) < 8) {
        effectiveApPass = DEFAULT_AP_PASS;
    }
    _apPass = effectiveApPass;
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.softAP(apSsid, effectiveApPass);
    Serial.printf("[WIFI] On-Demand SoftAP '%s' active for 5 mins. IP: %s\n", apSsid, WiFi.softAPIP().toString().c_str());
    
    dnsServer.start(53, "*", WiFi.softAPIP());
    
    webServer.on("/", HTTP_GET, handleRoot);
    webServer.on("/save", HTTP_POST, handleSave);
    webServer.onNotFound(handleRoot); // Captive portal redirect
    webServer.begin();
    Serial.println("[WIFI] Provisioning WebServer & Captive Portal started on port 80.");
    
    if (staSsid != nullptr && strlen(staSsid) > 0) {
        _staSsid = staSsid;
        _staPass = staPass ? staPass : "";
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
        Serial.printf("[WIFI] Station connecting to AP '%s'...\n", _staSsid.c_str());
        lastConnectRetry = millis();
    }
}

void WifiManager::update() {
    // 1. Process 5-minute AP timeout for power saving
    if (!apTimeoutReached && (millis() - bootTime >= 300000)) { // 300,000 ms = 5 minutes
        apTimeoutReached = true;
        dnsServer.stop();
        webServer.stop();
        WiFi.mode(WIFI_STA); // Switch to Station-only mode to stop RF AP broadcast
        Serial.println("[WIFI] 5-minute boot provisioning window expired. SoftAP disabled for power saving.");
    }

    if (!apTimeoutReached) {
        dnsServer.processNextRequest();
        webServer.handleClient();
    }
    
    if (WiFi.getMode() == WIFI_OFF || WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        return;
    }
    
    if (!_staSsid.isEmpty() && !connected && (millis() - lastConnectRetry >= 15000)) {
        lastConnectRetry = millis();
        Serial.printf("[WIFI] Attempting connection retry to AP '%s'...\n", _staSsid.c_str());
        WiFi.disconnect();
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
    }
}

bool WifiManager::isConnected() {
    return connected && (WiFi.status() == WL_CONNECTED);
}

void WifiManager::startScan() {
    WiFi.disconnect();
    WiFi.scanNetworks(true, false, false, 300);
}

int WifiManager::getScanStatus() {
    return WiFi.scanComplete();
}

String WifiManager::getScanResultsJson() {
    int n = WiFi.scanComplete();
    if (n < 0) return "[]";
    String json;
    json.reserve(32 + n * 64);
    json = "[";
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + escapeJson(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    WiFi.scanDelete();
    return json;
}

void WifiManager::connectTo(const char* ssid, const char* password) {
    _staSsid = ssid;
    _staPass = password ? password : "";
    connected = false;
    lastConnectRetry = millis();
    
    Serial.printf("[WIFI] Initiating dynamic connection to '%s'...\n", _staSsid.c_str());
    
    if (WiFi.getMode() != WIFI_AP_STA && !apTimeoutReached) {
        WiFi.mode(WIFI_AP_STA);
        if (_apSsid.length() > 0) {
            WiFi.softAP(_apSsid.c_str(), _apPass.c_str());
        }
        dnsServer.start(53, "*", WiFi.softAPIP());
    }
    WiFi.setSleep(false);
    WiFi.disconnect();
    WiFi.begin(_staSsid.c_str(), _staPass.c_str());
}

void WifiManager::stopWiFi() {
    connected = false;
    dnsServer.stop();
    webServer.stop();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI] Wi-Fi module disabled (WIFI_OFF).");
}

void WifiManager::startWiFi(const char* ssid, const char* password) {
    WiFi.mode(apTimeoutReached ? WIFI_STA : WIFI_AP_STA);
    WiFi.setSleep(false);
    
    if (!apTimeoutReached) {
        if (_apSsid.length() > 0) {
            WiFi.softAP(_apSsid.c_str(), _apPass.c_str());
        }
        dnsServer.start(53, "*", WiFi.softAPIP());
        webServer.begin();
    }
    
    if (ssid != nullptr && strlen(ssid) > 0) {
        _staSsid = ssid;
        _staPass = password ? password : "";
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
    }
    lastConnectRetry = millis();
}
