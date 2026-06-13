#include "ConfigManager.h"
#include <Preferences.h>
#include <Arduino.h>

void ConfigManager::loadConfig(BoxConfig& config) {
    Preferences prefs;
    // Open namespace "smartbox" in read-only mode (true)
    if (prefs.begin("smartbox", true)) {
        config.distThreshold = prefs.getFloat("dist", config.distThreshold);
        config.actuatorTime = prefs.getULong("act_time", config.actuatorTime);
        config.waitTime = prefs.getULong("wait_time", config.waitTime);
        config.cooldownTime = prefs.getULong("cool_time", config.cooldownTime);
        config.voltageShutdownLimit = prefs.getFloat("volt_lim", config.voltageShutdownLimit);
        config.currentStallLimit = prefs.getFloat("curr_lim", config.currentStallLimit);
        config.emergencyRecoveryTime = prefs.getULong("emrg_rec", config.emergencyRecoveryTime);
        prefs.end();
        Serial.println("[CONFIG] Configuration loaded from persistent Preferences.");
    } else {
        Serial.println("[CONFIG] No persistent Preferences found. Using defaults.");
    }

    // Self-healing check: If invalid 0 values are loaded (e.g. from previous corrupted NVS write)
    // restore the default safe parameters.
    if (config.actuatorTime < 500) {
        config.actuatorTime = 3800;
    }
    if (config.waitTime < 1000) {
        config.waitTime = 10000;
    }
    if (config.cooldownTime < 500) {
        config.cooldownTime = 3000;
    }
    if (config.distThreshold < 5.0f) {
        config.distThreshold = 50.0f;
    }
    if (config.voltageShutdownLimit < 5.0f) {
        config.voltageShutdownLimit = 11.3f;
    }
    if (config.currentStallLimit < 100.0f) {
        config.currentStallLimit = 3000.0f; // Dual actuator default
    }
    if (config.emergencyRecoveryTime < 1000) {
        config.emergencyRecoveryTime = 5000; // 5s default auto-recovery
    }

    Serial.printf("[CONFIG] Active parameters: dist=%.1f, act=%lu, wait=%lu, volt=%.1f, stall=%.1f\n",
                  config.distThreshold, config.actuatorTime, config.waitTime, 
                  config.voltageShutdownLimit, config.currentStallLimit);
}

void ConfigManager::saveConfig(const BoxConfig& config) {
    Preferences prefs;
    // Open namespace "smartbox" in read-write mode (false)
    prefs.begin("smartbox", false);
    
    prefs.putFloat("dist", config.distThreshold);
    prefs.putULong("act_time", config.actuatorTime);
    prefs.putULong("wait_time", config.waitTime);
    prefs.putULong("cool_time", config.cooldownTime);
    prefs.putFloat("volt_lim", config.voltageShutdownLimit);
    prefs.putFloat("curr_lim", config.currentStallLimit);
    prefs.putULong("emrg_rec", config.emergencyRecoveryTime);
    
    prefs.end();
    Serial.println("[CONFIG] Configuration saved to persistent Preferences.");
}

void ConfigManager::saveLidState(bool isOpen) {
    Preferences prefs;
    if (prefs.begin("smartbox", false)) {
        prefs.putBool("lid_open", isOpen);
        prefs.end();
        Serial.printf("[CONFIG] Lid state saved: %s\n", isOpen ? "OPEN" : "CLOSED");
    } else {
        Serial.println("[CONFIG] Failed to open Preferences to save lid state.");
    }
}

bool ConfigManager::loadLidState() {
    Preferences prefs;
    bool isOpen = false;
    if (prefs.begin("smartbox", true)) {
        isOpen = prefs.getBool("lid_open", false);
        prefs.end();
        Serial.printf("[CONFIG] Lid state loaded: %s\n", isOpen ? "OPEN" : "CLOSED");
    } else {
        Serial.println("[CONFIG] Failed to open Preferences to load lid state. Defaulting to CLOSED.");
    }
    return isOpen;
}
