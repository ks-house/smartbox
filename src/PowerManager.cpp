#include "PowerManager.h"
#include "WifiManager.h"
#include "ConfigManager.h"
#include <Arduino.h>
#include <time.h>

SmartBoxController* PowerManager::controllerPtr = nullptr;
unsigned long PowerManager::lastCheckTime = 0;

void PowerManager::init(SmartBoxController& controller) {
    controllerPtr = &controller;
    lastCheckTime = 0;
}

void PowerManager::update() {
    if (controllerPtr == nullptr) {
        return;
    }

    // Suspend power management transitions during active OTA
    if (controllerPtr->isOtaMode()) {
        return;
    }

    unsigned long now = millis();
    // Non-blocking check every 15 seconds
    if (now - lastCheckTime < 15000) {
        return;
    }
    lastCheckTime = now;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        // NTP time not synchronized yet
        return;
    }

    int currentHour = timeinfo.tm_hour;
    bool isNight = (currentHour >= 23 || currentHour < 6);

    if (isNight) {
        if (!controllerPtr->isNightSleepActive()) {
            Serial.println("[POWER] 진입: 야간 절전 모드 활성화. Wi-Fi OFF.");
            controllerPtr->setNightSleepMode(true);
            WifiManager::stopWiFi();
        }
    } else {
        if (controllerPtr->isNightSleepActive()) {
            Serial.println("[POWER] 해제: 주간 모드 전환. Wi-Fi 재연결 시도 중...");
            controllerPtr->setNightSleepMode(false);
            
            // Re-load saved credentials to connect
            String savedSsid = "";
            String savedPass = "";
            ConfigManager::loadWifiCredentials(savedSsid, savedPass);
            WifiManager::startWiFi(savedSsid.c_str(), savedPass.c_str());
        }
    }
}
