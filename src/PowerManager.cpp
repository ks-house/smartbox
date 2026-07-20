#include "PowerManager.h"
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

    // Proactive daily reboot scheduled 1 hour AFTER config.otaHour.
    // Design rationale: OTA check runs at config.otaHour (AutoOtaManager).
    //   - If OTA update found → ESP.restart() is called inside runOtaProcess()
    //   - If up-to-date     → returns to IDLE, PowerManager reboots 1 hour later
    // Keeping 1 hour gap prevents a race condition where PowerManager would reboot
    // the device before AutoOtaManager has a chance to start the OTA process.
    // The +1 offset also guarantees OTA has finished (typical OTA < 5 minutes).
    int rebootHour = (controllerPtr->getConfig().otaHour + 1) % 24;
    if (currentHour == rebootHour && controllerPtr->getCurrentState() == STATE_IDLE && !controllerPtr->isMotorRunning()) {
#ifndef NATIVE_BUILD
        Preferences prefs;
        if (prefs.begin("smartbox", false)) {
            int lastRebootDay = prefs.getInt("reboot_day", -1);
            if (lastRebootDay != timeinfo.tm_yday) {
                prefs.putInt("reboot_day", timeinfo.tm_yday);
                prefs.end();
                Serial.printf("[POWER] Daily maintenance reboot at %02d:00 (otaHour+1, day=%d). Restarting...\n",
                              rebootHour, timeinfo.tm_yday);
                delay(500);
                ESP.restart();
            }
            prefs.end();
        }
#endif
    }
    // Night Sleep mode has been removed by user request.
    // The device runs 24/7 without any time-based power gating.
}
