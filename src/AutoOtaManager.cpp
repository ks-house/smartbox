#include "AutoOtaManager.h"
#include "WifiManager.h"
#include <Arduino.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

SmartBoxController* AutoOtaManager::controllerPtr = nullptr;
unsigned long AutoOtaManager::lastScheduleCheck = 0;
int AutoOtaManager::lastOtaCheckDay = -1;

void AutoOtaManager::init(SmartBoxController& controller) {
    controllerPtr = &controller;
    lastScheduleCheck = 0;
    lastOtaCheckDay = -1;
}

void AutoOtaManager::update() {
    if (!WifiManager::isConnected()) {
        return;
    }
    
    unsigned long now = millis();
    // Non-blocking check every 30 seconds
    if (now - lastScheduleCheck < 30000) {
        return;
    }
    lastScheduleCheck = now;
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        // NTP not synchronized yet
        return;
    }
    
    // Auto-OTA at configured hour
    int scheduledHour = controllerPtr->getConfig().otaHour;
    if (timeinfo.tm_hour == scheduledHour) {
        if (lastOtaCheckDay != timeinfo.tm_mday) {
            lastOtaCheckDay = timeinfo.tm_mday;
            Serial.printf("[AUTO-OTA] Scheduled trigger time (%d:00 KST) reached. Launching AutoOtaTask...\n", scheduledHour);
            startOtaUpdate(false);
        }
    }
}

bool AutoOtaManager::startOtaUpdate(bool force) {
    if (controllerPtr == nullptr) {
        Serial.println("[OTA] Error: controllerPtr is null");
        return false;
    }
    
    BaseType_t result = xTaskCreate(
        otaTaskFunction,
        "auto_ota_task",
        16384, // 16KB stack size for TLS handshake
        (void*)force,
        1, // Low priority
        NULL
    );
    
    if (result == pdPASS) {
        Serial.println("[OTA] Background task created successfully.");
        return true;
    } else {
        Serial.println("[OTA] Failed to create background task.");
        return false;
    }
}

void AutoOtaManager::otaTaskFunction(void *pvParameters) {
    bool force = (bool)pvParameters;
    Serial.printf("[OTA] Background update task started (force=%s).\n", force ? "true" : "false");
    
    bool needsUpdate = false;
    
    if (!force) {
        Serial.println("[OTA] Step 1: Checking remote firmware version...");
        WiFiClientSecure client;
        client.setInsecure(); // Bypass ROOT CA validation
        
        HTTPClient http;
        http.begin(client, VERSION_URL);
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.printf("[OTA] version.json fetched successfully. Payload: %s\n", payload.c_str());
            
            String remoteVersion = parseJsonField(payload, "version");
            
            if (remoteVersion.isEmpty()) {
                Serial.println("[OTA] Error: Could not parse remote version from JSON.");
            } else {
                String currentVersion = controllerPtr->getFirmwareVersion();
                Serial.printf("[OTA] Current Version: %s, Remote Version: %s\n", currentVersion.c_str(), remoteVersion.c_str());
                if (remoteVersion != currentVersion) {
                    Serial.println("[OTA] Version mismatch detected. Update is required.");
                    needsUpdate = true;
                } else {
                    Serial.println("[OTA] System is up-to-date. No update needed.");
                }
            }
        } else {
            Serial.printf("[OTA] Failed to fetch version.json. HTTP Code: %d, Error: %s\n", 
                          httpCode, http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        // Forced update (triggered manually via Web Dashboard)
        needsUpdate = true;
    }
    
    if (needsUpdate) {
        Serial.println("[OTA] Step 2: Preparing hardware for flashing...");
        
        // Critical Safety Interlock
        controllerPtr->forceAllRelaysOff();
        Serial.println("[OTA] Pre-OTA Safety Interlock: All relays isolated.");
        
        controllerPtr->transitionTo(STATE_OTA_UPDATING);
        Serial.println("[OTA] FSM transitioned to STATE_OTA_UPDATING.");
        
        // Allow physical contacts to fully separate
        delay(500);
        
        Serial.printf("[OTA] Step 3: Fetching firmware binary from %s...\n", FIRMWARE_URL);
        
        WiFiClientSecure client;
        client.setInsecure();
        
        t_httpUpdate_return ret = httpUpdate.update(client, FIRMWARE_URL);
        
        if (ret == HTTP_UPDATE_FAILED) {
            Serial.printf("[OTA] HTTPS update FAILED! Error (%d): %s\n", 
                          httpUpdate.getLastError(), 
                          httpUpdate.getLastErrorString().c_str());
            
            // Failure recovery: restore FSM to STATE_IDLE
            controllerPtr->transitionTo(STATE_IDLE);
            Serial.println("[OTA] Recovered state to STATE_IDLE after update failure.");
        } else if (ret == HTTP_UPDATE_NO_UPDATES) {
            Serial.println("[OTA] HTTP update response: No updates available.");
            controllerPtr->transitionTo(STATE_IDLE);
        } else if (ret == HTTP_UPDATE_OK) {
            Serial.println("[OTA] HTTPS update SUCCESS! Rebooting device in 1.5 seconds...");
            delay(1500);
            ESP.restart();
        }
    }
    
    Serial.println("[OTA] Background update task completed and deleting itself.");
    vTaskDelete(NULL);
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
