#include "ConfigManager.h"
#include <Preferences.h>
#include <Arduino.h>

void ConfigManager::loadConfig(BoxConfig& config) {
    Preferences prefs;
    // Open namespace "smartbox" in read-only mode (true)
    prefs.begin("smartbox", true);
    
    config.distThreshold = prefs.getFloat("dist", config.distThreshold);
    config.actuatorTime = prefs.getULong("act_time", config.actuatorTime);
    config.waitTime = prefs.getULong("wait_time", config.waitTime);
    config.cooldownTime = prefs.getULong("cool_time", config.cooldownTime);
    config.voltageShutdownLimit = prefs.getFloat("volt_lim", config.voltageShutdownLimit);
    config.currentStallLimit = prefs.getFloat("curr_lim", config.currentStallLimit);
    
    prefs.end();
    Serial.println("[CONFIG] Configuration loaded from persistent Preferences.");
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
    
    prefs.end();
    Serial.println("[CONFIG] Configuration saved to persistent Preferences.");
}
