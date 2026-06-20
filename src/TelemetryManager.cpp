#include "TelemetryManager.h"
#include "WifiManager.h"
#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <InfluxDbClient.h>

SmartBoxController* TelemetryManager::controllerPtr = nullptr;
unsigned long TelemetryManager::lastSendTime = 0;

// Connection parameters as constants
static const char* INFLUXDB_URL = SECRET_INFLUXDB_URL;
static const char* INFLUXDB_TOKEN = SECRET_INFLUXDB_TOKEN;
static const char* INFLUXDB_ORG = SECRET_INFLUXDB_ORG;
static const char* INFLUXDB_BUCKET = SECRET_INFLUXDB_BUCKET;
static const char* DEVICE_TAG = "smartbox_01";
static const char* MEASUREMENT_NAME = "smartbox_status";


// Helper function to map states to strings for telemetry
static const char* stateToString(State state) {
    switch (state) {
        case STATE_IDLE: return "IDLE";
        case STATE_OPEN_START: return "OPEN_START";
        case STATE_OPENING: return "OPENING";
        case STATE_HOLD: return "HOLD";
        case STATE_CLOSE_START: return "CLOSE_START";
        case STATE_CLOSING: return "CLOSING";
        case STATE_EMERGENCY_STOP: return "EMERGENCY_STOP";
        case STATE_BATTERY_LOW_SHUTDOWN: return "BATTERY_LOW_SHUTDOWN";
        case STATE_STARTUP_OPEN: return "STARTUP_OPEN";
        case STATE_OTA_UPDATING: return "OTA_UPDATING";
        default: return "UNKNOWN";
    }
}

void TelemetryManager::init(SmartBoxController& controller) {
    controllerPtr = &controller;
    // Set to 0 to trigger telemetry as soon as WiFi connects and system is stable
    lastSendTime = 0;
}

void TelemetryManager::update() {
    if (controllerPtr == nullptr) {
        return;
    }

    if (controllerPtr->isNightSleepActive()) {
        return;
    }

    // Check if 30 seconds (30,000ms) have passed
    unsigned long now = millis();
    if (now - lastSendTime < 30000) {
        return;
    }

    // Immediately log and skip if WiFi is not connected
    if (!WifiManager::isConnected()) {
        Serial.println("[TELEMETRY] Skip transmission: WiFi not connected.");
        lastSendTime = now; // prevent flooding, retry in 30s
        return;
    }

    // Skip transmission if the motor is active or OTA update is in progress
    if (!controllerPtr->canSendTelemetry()) {
        Serial.printf("[TELEMETRY] Skip transmission: System in active/unsafe state (State: %d, Motor Running: %s).\n", 
                      (int)controllerPtr->getCurrentState(), 
                      controllerPtr->isMotorRunning() ? "true" : "false");
        lastSendTime = now; // retry in 30s
        return;
    }

    // Log start of transmission attempt
    Serial.println("[TELEMETRY] InfluxDB 전송 시도 중...");
    lastSendTime = now;

    // Capture telemetry snapshot safely
    float battery_v = controllerPtr->getBatteryVoltage();
    float distance_cm = controllerPtr->getDistance();
    int state = (int)controllerPtr->getCurrentState();
    int wifi_rssi = WiFi.RSSI();

    Serial.printf("[TELEMETRY] Initializing InfluxDB connection to: %s\n", INFLUXDB_URL);

    // Initialize local client instance
    InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

    // Bypassing certificate checks for DDNS Synology connection
    client.setInsecure();

    // Construct the measurement point
    Point pointDevice(MEASUREMENT_NAME);
    pointDevice.addTag("device", DEVICE_TAG);

    pointDevice.addField("battery_v", battery_v);
    pointDevice.addField("distance_cm", distance_cm);
    pointDevice.addField("state", state);
    pointDevice.addField("state_str", stateToString((State)state));
    pointDevice.addField("wifi_rssi", wifi_rssi);

    Serial.printf("[TELEMETRY] Transmitting fields -> battery_v: %.2f V, distance_cm: %.1f cm, state: %d (%s), wifi_rssi: %d dBm\n",
                  battery_v, distance_cm, state, stateToString((State)state), wifi_rssi);

    // Submit telemetry
    if (client.writePoint(pointDevice)) {
        Serial.printf("[TELEMETRY] 전송 성공! (HTTP %d)\n", client.getLastStatusCode());
    } else {
        Serial.printf("[TELEMETRY] 전송 실패! Error Code: %d, Message: %s\n", 
                      client.getLastStatusCode(), client.getLastErrorMessage().c_str());
    }
}
