#include "AutoOtaManager.h"
#include "WifiManager.h"
#include <Arduino.h>
#include <time.h>
#include <cstring>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "secrets.h"

// Older local headers retain private-network OTA; production CI requires these.
#ifndef SECRET_OTA_USER
#define SECRET_OTA_USER ""
#endif
#ifndef SECRET_OTA_PASS
#define SECRET_OTA_PASS ""
#endif

#ifndef NATIVE_BUILD
#include <esp_task_wdt.h>
#endif

SmartBoxController* AutoOtaManager::controllerPtr = nullptr;
unsigned long AutoOtaManager::lastScheduleCheck = 0;
int AutoOtaManager::lastOtaRunDay = -1;
unsigned long AutoOtaManager::lastOtaRunTime = 0; // 0 = never run
bool AutoOtaManager::otaForceRequested = false;
bool AutoOtaManager::otaRecoveryRequested = false;
std::mutex AutoOtaManager::requestMutex;
uint32_t AutoOtaManager::recoveryRequestAt=0,AutoOtaManager::recoveryRequestTtl=0;
bool AutoOtaManager::recoveryRequestFresh(){return uint32_t(millis()-recoveryRequestAt)<recoveryRequestTtl;}
std::atomic<bool> AutoOtaManager::otaInProgress{false};

std::atomic<AutoOtaManager::OtaState> AutoOtaManager::otaState{AutoOtaManager::OTA_STATE_IDLE};
char AutoOtaManager::otaErrorMessage[128] = "";
std::atomic<AutoOtaManager::OtaPhase> AutoOtaManager::otaPhase{AutoOtaManager::OTA_PHASE_IDLE};
std::atomic<uint32_t> AutoOtaManager::otaAttempts{0};
std::atomic<int> AutoOtaManager::otaManifestHttpCode{0};
std::atomic<int> AutoOtaManager::otaUpdaterError{0};

AutoOtaManager::OtaDiagnostics AutoOtaManager::getDiagnostics() {
    std::lock_guard<std::mutex> lock(requestMutex);
    const char* phase = "IDLE";
    switch (otaPhase) {
        case OTA_PHASE_VERSION: phase = "VERSION"; break;
        case OTA_PHASE_FIRMWARE: phase = "FIRMWARE"; break;
        case OTA_PHASE_REBOOT_PENDING: phase = "REBOOT_PENDING"; break;
        default: break;
    }
    // These codes describe the last started attempt. A queued request is separate.
    return {otaAttempts, phase, otaManifestHttpCode, otaUpdaterError, otaForceRequested};
}


void AutoOtaManager::init(SmartBoxController& controller) {
    std::lock_guard<std::mutex> lock(requestMutex);
    controllerPtr = &controller;
    Serial.println("[OTA] HTTPS authentication client schema=1");
    lastScheduleCheck = 0;
    lastOtaRunTime = 0;
    lastOtaRunDay = -1;
    otaForceRequested = false;
    otaRecoveryRequested = false;
    otaInProgress = false;
    otaState = OTA_STATE_IDLE;
    otaPhase = OTA_PHASE_IDLE;
    otaAttempts = 0;
    otaManifestHttpCode = 0;
    otaUpdaterError = 0;
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
    
    const bool normalReady=controllerPtr->canStartOta();
    bool recovery=false;
    if(takeManualRequest(normalReady,recovery)) {
        if(recovery && !recoveryRequestFresh()) {
            otaState=OTA_STATE_FAILED;strncpy(otaErrorMessage,"RECOVERY_REQUEST_EXPIRED",sizeof(otaErrorMessage)-1);otaInProgress=false;return;
        }
        runOtaProcess(true,recovery);return;
    }
    if(!normalReady)return;

    unsigned long now = millis();
    // Non-blocking check every 30 seconds
    if (now - lastScheduleCheck < 30000) {
        return;
    }
    lastScheduleCheck = now;

    // --- First-boot check: always run OTA version check once after bootup ---
    if (lastOtaRunTime == 0) {
        if(!claimAutomaticRequest())return;
        lastOtaRunTime = now;
        struct tm bootTimeInfo;
        if (getLocalTime(&bootTimeInfo, 0)) lastOtaRunDay = bootTimeInfo.tm_year * 400 + bootTimeInfo.tm_yday;
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
    if (timeinfo.tm_hour >= configuredOtaHour) {
        // Use day-of-year as guard to run only once per calendar day
        const int calendarDay = timeinfo.tm_year * 400 + timeinfo.tm_yday;
        if (lastOtaRunDay != calendarDay) {
            if(!claimAutomaticRequest())return;
            lastOtaRunDay = calendarDay;
            lastOtaRunTime = now;
            Serial.printf("[AUTO-OTA] Scheduled daily OTA check at %02d:00 (config.otaHour=%d, day=%d).\n",
                          configuredOtaHour, configuredOtaHour, timeinfo.tm_yday);
            runOtaProcess(false);
        }
    }
}

bool AutoOtaManager::takeManualRequest(bool normalReady,bool& recovery) {
    std::lock_guard<std::mutex> lock(requestMutex);
    if(otaInProgress || !otaForceRequested || (!otaRecoveryRequested && !normalReady))return false;
    recovery=otaRecoveryRequested;otaForceRequested=false;otaRecoveryRequested=false;otaInProgress=true;return true;
}
bool AutoOtaManager::claimAutomaticRequest() {
    std::lock_guard<std::mutex> lock(requestMutex);
    if(otaInProgress || otaForceRequested)return false;otaInProgress=true;return true;
}
bool AutoOtaManager::startOtaUpdate(bool force,bool sensorFaultRecovery,uint32_t recoveryTtlMs) {
    if(!controllerPtr || !force || (sensorFaultRecovery && (recoveryTtlMs==0 || recoveryTtlMs>120000)))return false;
    // Never hold the request lock while acquiring the controller/GPIO lock.
    if(!controllerPtr->canStartOta(sensorFaultRecovery))return false;
    std::lock_guard<std::mutex> lock(requestMutex);
    if(otaInProgress)return false;
    if(otaForceRequested && !(sensorFaultRecovery && (!otaRecoveryRequested || !recoveryRequestFresh())))return false;
    otaRecoveryRequested=sensorFaultRecovery;
    if(sensorFaultRecovery){recoveryRequestAt=millis();recoveryRequestTtl=recoveryTtlMs;}
    otaForceRequested=true;otaState=OTA_STATE_CHECKING;std::memset(otaErrorMessage,0,sizeof(otaErrorMessage));return true;
}

void AutoOtaManager::runOtaProcess(bool force, bool sensorFaultRecovery) {
    if(sensorFaultRecovery) {
        if(!recoveryRequestFresh() || !controllerPtr->tryBeginOta(true)) {
            otaState=OTA_STATE_FAILED;strncpy(otaErrorMessage,"RECOVERY_NOT_ADMITTED",sizeof(otaErrorMessage)-1);otaInProgress=false;return;
        }
    } else if(!controllerPtr->canStartOta()){otaState=OTA_STATE_FAILED;otaInProgress=false;return;}
    otaAttempts = otaAttempts + 1;
    otaPhase = OTA_PHASE_VERSION;
    otaManifestHttpCode = 0;
    otaUpdaterError = 0;
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
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (SECRET_OTA_USER[0] && SECRET_OTA_PASS[0]) {
        http.setAuthorization(SECRET_OTA_USER, SECRET_OTA_PASS);
    }
    int httpCode = http.GET();
    otaManifestHttpCode = httpCode;
    
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
    
    if (needsUpdate && !(sensorFaultRecovery ? (recoveryRequestFresh() && controllerPtr->canContinueOta()) : controllerPtr->canStartOta())) {
        otaState = OTA_STATE_FAILED;
        strncpy(otaErrorMessage, "POWER_NOT_READY", sizeof(otaErrorMessage)-1);
        if(sensorFaultRecovery)controllerPtr->finishOtaWithoutReboot();
        otaInProgress=false;return;
    }
    if (needsUpdate) {
        Serial.println("[OTA] Step 2: Preparing hardware for flashing...");
        
        // Critical Safety Interlock
        if (!sensorFaultRecovery && !controllerPtr->tryBeginOta()) {
            otaState = OTA_STATE_FAILED;
            strncpy(otaErrorMessage, "POWER_OR_BUSY", sizeof(otaErrorMessage)-1);
            otaInProgress=false;return;
        }
        Serial.println("[OTA] Pre-OTA Safety Interlock: All relays isolated.");
        Serial.println("[OTA] FSM transitioned to STATE_OTA_UPDATING.");
        
        // Wait until motor is fully stopped and contacts separated
        unsigned long startWait = millis();
        while (controllerPtr->getMotorCurrent() > 50.0f && (millis() - startWait < 1000)) {
            delay(50);
        }
        delay(500); // Allow physical contacts to fully separate
        if (!controllerPtr->canContinueOta() || (sensorFaultRecovery && !recoveryRequestFresh())) {
            otaState = OTA_STATE_FAILED;
            strncpy(otaErrorMessage, "POWER_NOT_READY", sizeof(otaErrorMessage)-1);
            controllerPtr->finishOtaWithoutReboot();
            otaInProgress = false;
            return;
        }
        
#ifndef NATIVE_BUILD
        // Unsubscribe current task (NetworkTask) from WDT during flashing
        esp_task_wdt_delete(NULL);
        Serial.println("[OTA] NetworkTask unsubscribed from WDT for flashing.");
#endif

        Serial.printf("[OTA] Step 3: Fetching firmware binary from %s...\n", FIRMWARE_URL);
        
        WiFiClientSecure client2;
        client2.setCACert(SECRET_ROOT_CA_CERT); // TLS Root CA verification
        
        httpUpdate.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        httpUpdate.setAuthorization(SECRET_OTA_USER, SECRET_OTA_PASS);
        if (!controllerPtr->canContinueOta() || (sensorFaultRecovery && !recoveryRequestFresh())) {
#ifndef NATIVE_BUILD
            esp_task_wdt_add(NULL);
#endif
            otaState = OTA_STATE_FAILED;
            strncpy(otaErrorMessage, "POWER_NOT_READY", sizeof(otaErrorMessage)-1);
            controllerPtr->finishOtaWithoutReboot();
            otaInProgress = false;
            return;
        }
        otaPhase = OTA_PHASE_FIRMWARE;
        t_httpUpdate_return ret = httpUpdate.update(client2, FIRMWARE_URL);
        
        if (ret == HTTP_UPDATE_FAILED) {
            otaUpdaterError = httpUpdate.getLastError();
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
            controllerPtr->finishOtaWithoutReboot();
            Serial.println("[OTA] Recovered state to STATE_IDLE after update failure.");
            
        } else if (ret == HTTP_UPDATE_NO_UPDATES) {
            Serial.println("[OTA] HTTP update response: No updates available.");
            otaState = OTA_STATE_UP_TO_DATE;
#ifndef NATIVE_BUILD
            // BUG-07 fix: re-subscribe WDT BEFORE transitionTo()
            esp_task_wdt_add(NULL);
            Serial.println("[OTA] NetworkTask re-subscribed to WDT before no-update recovery.");
#endif
            controllerPtr->finishOtaWithoutReboot();
        } else if (ret == HTTP_UPDATE_OK) {
            Serial.println("[OTA] HTTPS update SUCCESS! Rebooting device in 1.5 seconds...");
            otaState = OTA_STATE_SUCCESS;
            otaPhase = OTA_PHASE_REBOOT_PENDING;
            delay(1500);
            ESP.restart();
        }
    }
    
    if(sensorFaultRecovery && controllerPtr->isOtaMode())controllerPtr->finishOtaWithoutReboot();
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
