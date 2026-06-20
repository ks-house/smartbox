#include "TelemetryManager.h"
#include "WifiManager.h"
#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <InfluxDbClient.h>

SmartBoxController* TelemetryManager::controllerPtr = nullptr;
unsigned long TelemetryManager::lastSendTime = 0;
TelemetryData TelemetryManager::eventBuffer[MAX_BATCH_SIZE];
int TelemetryManager::bufferCount = 0;
bool TelemetryManager::wasMotorRunning = false;
unsigned long TelemetryManager::lastSampleTime = 0;

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
        case STATE_MAINTENANCE: return "MAINTENANCE";
        default: return "UNKNOWN";
    }
}

void TelemetryManager::telemetryTaskFunction(void* pvParameters) {
    BatchPayload* payload = (BatchPayload*)pvParameters;
    if (payload == nullptr) {
        vTaskDelete(NULL);
        return;
    }
    
    Serial.printf("[TELEMETRY-ASYNC] Initializing batch transmission to InfluxDB, size: %d\n", payload->count);
    
    InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);
    client.setInsecure();
    
    bool success = true;
    unsigned long baseTime = (payload->count > 0) ? payload->data[0].timestamp_ms : 0;
    
    for (int i = 0; i < payload->count; i++) {
        const TelemetryData& d = payload->data[i];
        
        Point pointDevice(MEASUREMENT_NAME);
        pointDevice.addTag("device", DEVICE_TAG);
        pointDevice.addTag("type", "batch");
        
        pointDevice.addField("battery_v", d.battery_v);
        pointDevice.addField("distance_cm", d.distance_cm);
        pointDevice.addField("motor_current", d.motor_current);
        pointDevice.addField("state", d.state);
        pointDevice.addField("state_str", stateToString((State)d.state));
        
        unsigned long offset = d.timestamp_ms - baseTime;
        pointDevice.addField("offset_ms", (int)offset);
        
        Serial.printf("[TELEMETRY-ASYNC] Batch point %d/%d -> offset: %d ms, batt: %.2f V, curr: %.1f mA, state: %s\n",
                      i + 1, payload->count, (int)offset, d.battery_v, d.motor_current, stateToString((State)d.state));
                      
        if (!client.writePoint(pointDevice)) {
            success = false;
            Serial.printf("[TELEMETRY-ASYNC] Batch point %d write failed! HTTP code: %d, Error: %s\n",
                          i + 1, client.getLastStatusCode(), client.getLastErrorMessage().c_str());
        }
    }
    
    if (success) {
        Serial.printf("[TELEMETRY-ASYNC] Asynchronous batch write of %d points completed successfully.\n", payload->count);
    } else {
        Serial.println("[TELEMETRY-ASYNC] Asynchronous batch write completed with some failures.");
    }
    
    // Safely reclaim allocated memory
    delete[] payload->data;
    delete payload;
    
    vTaskDelete(NULL);
}

void TelemetryManager::init(SmartBoxController& controller) {
    controllerPtr = &controller;
    lastSendTime = 0;
    bufferCount = 0;
    wasMotorRunning = false;
    lastSampleTime = 0;
}

void TelemetryManager::update() {
    if (controllerPtr == nullptr) {
        return;
    }

    if (controllerPtr->isNightSleepActive()) {
        return;
    }

    bool isMotor = controllerPtr->isMotorRunning();

    if (isMotor) {
        wasMotorRunning = true;
        unsigned long now = millis();
        if (now - lastSampleTime >= 500) {
            lastSampleTime = now;
            
            TelemetryData data;
            data.battery_v = controllerPtr->getBatteryVoltage();
            data.distance_cm = controllerPtr->getDistance();
            data.motor_current = controllerPtr->getMotorCurrent();
            data.state = (int)controllerPtr->getCurrentState();
            data.timestamp_ms = now;
            
            if (bufferCount < MAX_BATCH_SIZE) {
                eventBuffer[bufferCount++] = data;
            } else {
                // Ring Buffer: shift left to discard the oldest and keep the newest chronologically
                for (int i = 1; i < MAX_BATCH_SIZE; i++) {
                    eventBuffer[i - 1] = eventBuffer[i];
                }
                eventBuffer[MAX_BATCH_SIZE - 1] = data;
            }
            Serial.printf("[TELEMETRY] High-frequency sample saved. Buffer count: %d, State: %d, Current: %.1f mA\n",
                          bufferCount, data.state, data.motor_current);
        }
    } else {
        // Motor is NOT running. Check if it was running previously
        if (wasMotorRunning) {
            wasMotorRunning = false;
            if (bufferCount > 0) {
                if (controllerPtr->canSendTelemetry() && WifiManager::isConnected()) {
                    Serial.printf("[TELEMETRY] Motor stopped. Spawning async batch transmission of %d points...\n", bufferCount);
                    
                    BatchPayload* payload = new BatchPayload();
                    payload->count = bufferCount;
                    payload->data = new TelemetryData[bufferCount];
                    for (int i = 0; i < bufferCount; i++) {
                        payload->data[i] = eventBuffer[i];
                    }
                    
                    // Reset buffer count immediately
                    bufferCount = 0;
                    
                    BaseType_t res = xTaskCreate(
                        telemetryTaskFunction,
                        "BatchTelemetry",
                        8192,
                        payload,
                        1, // Low priority background task
                        NULL
                    );
                    
                    if (res != pdPASS) {
                        Serial.println("[TELEMETRY] Failed to create BatchTelemetry task. Reclaiming memory.");
                        delete[] payload->data;
                        delete payload;
                    }
                } else {
                    Serial.println("[TELEMETRY] Motor stopped, but cannot send telemetry (WiFi offline or sleep/unsafe state). Clearing buffer.");
                    bufferCount = 0;
                }
            }
        }
    }

    // Normal heartbeat telemetry every 30 seconds
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
    pointDevice.addTag("type", "heartbeat");

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
