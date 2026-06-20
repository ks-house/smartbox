#include "PowerManager.h"
#include "WifiManager.h"
#include "ConfigManager.h"
#include <Arduino.h>
#include <time.h>
#ifndef NATIVE_BUILD
#include <Preferences.h>
#endif

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

    // Proactive daily reboot scheduling at 04:00 AM
    if (currentHour == 4 && controllerPtr->getCurrentState() == STATE_IDLE && !controllerPtr->isMotorRunning()) {
#ifndef NATIVE_BUILD
        Preferences prefs;
        if (prefs.begin("smartbox", false)) {
            int lastRebootDay = prefs.getInt("reboot_day", -1);
            if (lastRebootDay != timeinfo.tm_yday) {
                prefs.putInt("reboot_day", timeinfo.tm_yday);
                prefs.end();
                Serial.printf("[POWER] Proactive scheduled reboot (04:00 AM, day of year: %d). Restarting...\n", timeinfo.tm_yday);
                delay(500);
                ESP.restart();
            }
            prefs.end();
        }
#endif
    }

    bool isNight = (currentHour >= 23 || currentHour < 6);

    if (isNight) {
        if (!controllerPtr->isNightSleepActive()) {
            Serial.println("[POWER] 진입: 야간 절전 모드 활성화. Wi-Fi OFF.");
            controllerPtr->setNightSleepMode(true);
            WifiManager::stopWiFi();
#ifndef NATIVE_BUILD
            setCpuFrequencyMhz(80);
            Serial.println("[POWER] CPU Frequency scaled to 80 MHz");
#endif
        }
    } else {
        if (controllerPtr->isNightSleepActive()) {
            Serial.println("[POWER] 해제: 주간 모드 전환. Wi-Fi 재연결 시도 중...");
#ifndef NATIVE_BUILD
            setCpuFrequencyMhz(160);
            Serial.println("[POWER] CPU Frequency scaled to 160 MHz");
#endif
            controllerPtr->setNightSleepMode(false);
            
            // Re-load saved credentials to connect
            String savedSsid = "";
            String savedPass = "";
            ConfigManager::loadWifiCredentials(savedSsid, savedPass);
            WifiManager::startWiFi(savedSsid.c_str(), savedPass.c_str());
        }
    }
}
