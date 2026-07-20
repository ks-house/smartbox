#include "AutoOtaManager.h"
#include "WifiManager.h"
#include <Arduino.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "secrets.h"

#ifndef NATIVE_BUILD
#include <esp_task_wdt.h>
#endif

SmartBoxController* AutoOtaManager::controllerPtr = nullptr;
unsigned long AutoOtaManager::lastScheduleCheck = 0;
unsigned long AutoOtaManager::lastOtaRunTime = 0; // 0 = never run
volatile bool AutoOtaManager::otaForceRequested = false;
volatile bool AutoOtaManager::otaInProgress = false;

volatile AutoOtaManager::OtaState AutoOtaManager::otaState = AutoOtaManager::OTA_STATE_IDLE;
char AutoOtaManager::otaErrorMessage[128] = "";

void AutoOtaManager::init(SmartBoxController& controller) {
    controllerPtr = &controller;
    lastScheduleCheck = 0;
    lastOtaRunTime = 0;
    otaForceRequested = false;
    otaInProgress = false;
    otaState = OTA_STATE_IDLE;
    memset(otaErrorMessage, 0, sizeof(otaErrorMessage));
}

String AutoOtaManager::getOtaStateString() {
    switch (otaState) {
        case OTA_STATE_IDLE: return "IDLE";
        case OTA_STATE_CHECKING: return "CHECKING";
        case OTA_STATE_UP_TO_DATE: return "UP_TO_DATE";
        case OTA_STATE_UPDATING: return "UPDATING";
        case OTA_STATE_FAILED: return "FAILED";
        case OTA_STATE_SUCCESS: return "SUCCESS";
        default: return "UNKNOWN";
    }
}

void AutoOtaManager::update() {
    if (controllerPtr == nullptr) {
        return;
    }
    if (!WifiManager::isConnected()) {
        return;
    }
    
    // Check if dynamic force update requested
    if (otaForceRequested) {
        otaForceRequested = false;
        runOtaProcess(true);
        return;
    }
    
    unsigned long now = millis();
    // Non-blocking check every 30 seconds
    if (now - lastScheduleCheck < 30000) {
        return;
    }
    lastScheduleCheck = now;

    // --- First-boot check: always run OTA version check once after bootup ---
    if (lastOtaRunTime == 0) {
        lastOtaRunTime = now;
        Serial.println("[AUTO-OTA] First boot: running initial OTA version check...");
        runOtaProcess(false);
        return;
    }

    // --- Scheduled daily OTA: run at config.otaHour (NTP-based, once per day) ---
    // BUG-O1 fix: config.otaHour was stored but never used in scheduling.
    // Now we read NTP time and trigger OTA when currentHour == config.otaHour,
    // using a per-day guard (lastOtaRunDay) to ensure exactly 1 run per day.
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        // NTP not yet synchronized, skip
        return;
    }

    int configuredOtaHour = controllerPtr->getConfig().otaHour;
    if (timeinfo.tm_hour == configuredOtaHour) {
        // Use day-of-year as guard to run only once per calendar day
        static int lastOtaRunDay = -1;
        if (lastOtaRunDay != timeinfo.tm_yday) {
            lastOtaRunDay = timeinfo.tm_yday;
            lastOtaRunTime = now;
            Serial.printf("[AUTO-OTA] Scheduled daily OTA check at %02d:00 (config.otaHour=%d, day=%d).\n",
                          configuredOtaHour, configuredOtaHour, timeinfo.tm_yday);
            runOtaProcess(false);
        }
    }
}

bool AutoOtaManager::startOtaUpdate(bool force) {
    if (controllerPtr == nullptr) {
        Serial.println("[OTA] Error: controllerPtr is null");
        return false;
    }
    
    if (force) {
        otaForceRequested = true;
        otaState = OTA_STATE_CHECKING;
        memset(otaErrorMessage, 0, sizeof(otaErrorMessage));
        Serial.println("[OTA] Force OTA update requested via flag.");
        return true;
    }
    return false;
}

void AutoOtaManager::runOtaProcess(bool force) {
    Serial.printf("[OTA] Background update process started (force=%s).\n", force ? "true" : "false");
    otaInProgress = true;
    otaState = OTA_STATE_CHECKING;
    memset(otaErrorMessage, 0, sizeof(otaErrorMessage));
    
    bool needsUpdate = false;
    
    Serial.println("[OTA] Step 1: Checking remote firmware version...");
    WiFiClientSecure client;
    client.setCACert(SECRET_ROOT_CA_CERT); // TLS Root CA verification
    
    HTTPClient http;
    http.begin(client, VERSION_URL);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.printf("[OTA] version.json fetched successfully. Payload: %s\n", payload.c_str());
        
        String remoteVersion = parseJsonField(payload, "version");
        
        if (remoteVersion.isEmpty()) {
            Serial.println("[OTA] Error: Could not parse remote version from JSON.");
            otaState = OTA_STATE_FAILED;
            strncpy(otaErrorMessage, "Could not parse remote version from JSON", sizeof(otaErrorMessage) - 1);
        } else {
            String currentVersion = controllerPtr->getFirmwareVersion();
            Serial.printf("[OTA] Current Version: %s, Remote Version: %s\n", currentVersion.c_str(), remoteVersion.c_str());
            if (remoteVersion != currentVersion) {
                Serial.println("[OTA] Version mismatch detected. Update is required.");
                needsUpdate = true;
                otaState = OTA_STATE_UPDATING;
            } else {
                Serial.println("[OTA] System is up-to-date. No update needed.");
                otaState = OTA_STATE_UP_TO_DATE;
            }
        }
    } else {
        Serial.printf("[OTA] Failed to fetch version.json. HTTP Code: %d, Error: %s\n", 
                      httpCode, http.errorToString(httpCode).c_str());
        otaState = OTA_STATE_FAILED;
        String errStr = "Fetch version failed: HTTP " + String(httpCode);
        strncpy(otaErrorMessage, errStr.c_str(), sizeof(otaErrorMessage) - 1);
    }
    http.end();
    
    if (needsUpdate) {
        Serial.println("[OTA] Step 2: Preparing hardware for flashing...");
        
        // Critical Safety Interlock
        controllerPtr->forceAllRelaysOff();
        Serial.println("[OTA] Pre-OTA Safety Interlock: All relays isolated.");
        
        controllerPtr->transitionTo(STATE_OTA_UPDATING);
        Serial.println("[OTA] FSM transitioned to STATE_OTA_UPDATING.");
        
        // Wait until motor is fully stopped and contacts separated
        unsigned long startWait = millis();
        while (controllerPtr->getMotorCurrent() > 50.0f && (millis() - startWait < 1000)) {
            delay(50);
        }
        delay(500); // Allow physical contacts to fully separate
        
#ifndef NATIVE_BUILD
        // Unsubscribe current task (NetworkTask) from WDT during flashing
        esp_task_wdt_delete(NULL);
        Serial.println("[OTA] NetworkTask unsubscribed from WDT for flashing.");
#endif

        Serial.printf("[OTA] Step 3: Fetching firmware binary from %s...\n", FIRMWARE_URL);
        
        WiFiClientSecure client2;
        client2.setCACert(SECRET_ROOT_CA_CERT); // TLS Root CA verification
        
        t_httpUpdate_return ret = httpUpdate.update(client2, FIRMWARE_URL);
        
        if (ret == HTTP_UPDATE_FAILED) {
            Serial.printf("[OTA] HTTPS update FAILED! Error (%d): %s\n", 
                          httpUpdate.getLastError(), 
                          httpUpdate.getLastErrorString().c_str());
            
            otaState = OTA_STATE_FAILED;
            String errStr = "Update failed: " + httpUpdate.getLastErrorString();
            strncpy(otaErrorMessage, errStr.c_str(), sizeof(otaErrorMessage) - 1);
            
#ifndef NATIVE_BUILD
            // BUG-07 fix: re-subscribe WDT BEFORE transitionTo() so stateCallback runs under WDT protection
            esp_task_wdt_add(NULL);
            Serial.println("[OTA] NetworkTask re-subscribed to WDT before state recovery.");
#endif
            // Failure recovery: restore FSM to STATE_IDLE
            controllerPtr->transitionTo(STATE_IDLE);
            Serial.println("[OTA] Recovered state to STATE_IDLE after update failure.");
            
        } else if (ret == HTTP_UPDATE_NO_UPDATES) {
            Serial.println("[OTA] HTTP update response: No updates available.");
            otaState = OTA_STATE_UP_TO_DATE;
#ifndef NATIVE_BUILD
            // BUG-07 fix: re-subscribe WDT BEFORE transitionTo()
            esp_task_wdt_add(NULL);
            Serial.println("[OTA] NetworkTask re-subscribed to WDT before no-update recovery.");
#endif
            controllerPtr->transitionTo(STATE_IDLE);
        } else if (ret == HTTP_UPDATE_OK) {
            Serial.println("[OTA] HTTPS update SUCCESS! Rebooting device in 1.5 seconds...");
            otaState = OTA_STATE_SUCCESS;
            delay(1500);
            ESP.restart();
        }
    }
    
    otaInProgress = false;
    Serial.println("[OTA] Update process completed.");
}

String AutoOtaManager::parseJsonField(const String& json, const String& key) {
    int keyIdx = json.indexOf("\"" + key + "\"");
    if (keyIdx == -1) return "";
    
    int colonIdx = json.indexOf(":", keyIdx);
    if (colonIdx == -1) return "";
    
    int startIdx = json.indexOf("\"", colonIdx);
    if (startIdx == -1) return "";
    
    int endIdx = json.indexOf("\"", startIdx + 1);
    if (endIdx == -1) return "";
    
    return json.substring(startIdx + 1, endIdx);
}
