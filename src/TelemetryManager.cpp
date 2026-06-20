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

// Buffer for periodic heartbeat telemetry
TelemetryData TelemetryManager::heartbeatBuffer[60];
int TelemetryManager::heartbeatCount = 0;
unsigned long TelemetryManager::lastHeartbeatSampleTime = 0;

// Diagnostic Store & Forward state
int TelemetryManager::pendingFailedTxCount = 0;
String TelemetryManager::lastNetworkError = "";
std::mutex TelemetryManager::diagMutex;

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
    
    Serial.printf("[TELEMETRY-ASYNC] Initializing %s batch transmission to InfluxDB, size: %d\n", payload->type, payload->count);
    
    InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);
    client.setInsecure();
    
    bool success = true;
    unsigned long baseTime = (payload->count > 0) ? payload->data[0].timestamp_ms : 0;
    
    // Retrieve pending diagnostic failure info
    int failedCount = 0;
    String errMsg = "";
    {
        std::lock_guard<std::mutex> lock(diagMutex);
        failedCount = pendingFailedTxCount;
        errMsg = lastNetworkError;
    }
    
    for (int i = 0; i < payload->count; i++) {
        const TelemetryData& d = payload->data[i];
        
        Point pointDevice(MEASUREMENT_NAME);
        pointDevice.addTag("device", DEVICE_TAG);
        pointDevice.addTag("type", payload->type);
        
        pointDevice.addField("battery_v", d.battery_v);
        pointDevice.addField("distance_cm", d.distance_cm);
        pointDevice.addField("motor_current", d.motor_current);
        pointDevice.addField("state", d.state);
        pointDevice.addField("state_str", stateToString((State)d.state));
        
        unsigned long offset = d.timestamp_ms - baseTime;
        pointDevice.addField("offset_ms", (int)offset);
        
        // Append diagnostic data to the first point if there are any failures cached
        if (i == 0 && failedCount > 0) {
            pointDevice.addField("failed_tx_count", failedCount);
            pointDevice.addField("last_error_msg", errMsg.c_str());
            Serial.printf("[TELEMETRY-ASYNC] Appended diagnostics: failed_tx_count=%d, last_error_msg=%s\n",
                          failedCount, errMsg.c_str());
        }
        
        Serial.printf("[TELEMETRY-ASYNC] Batch point %d/%d -> offset: %d ms, batt: %.2f V, curr: %.1f mA, state: %s\n",
                      i + 1, payload->count, (int)offset, d.battery_v, d.motor_current, stateToString((State)d.state));
                      
        if (!client.writePoint(pointDevice)) {
            success = false;
            String errorMsg = "HTTP " + String(client.getLastStatusCode()) + ": " + client.getLastErrorMessage();
            Serial.printf("[TELEMETRY-ASYNC] Batch point %d write failed! HTTP code: %d, Error: %s\n",
                          i + 1, client.getLastStatusCode(), client.getLastErrorMessage().c_str());
            
            std::lock_guard<std::mutex> lock(diagMutex);
            pendingFailedTxCount++;
            lastNetworkError = errorMsg;
        }
    }
    
    if (success) {
        Serial.printf("[TELEMETRY-ASYNC] Asynchronous batch write of %d points completed successfully.\n", payload->count);
        
        std::lock_guard<std::mutex> lock(diagMutex);
        pendingFailedTxCount = 0;
        lastNetworkError = "";
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
    heartbeatCount = 0;
    lastHeartbeatSampleTime = 0;
    
    std::lock_guard<std::mutex> lock(diagMutex);
    pendingFailedTxCount = 0;
    lastNetworkError = "";
}

void TelemetryManager::update() {
    if (controllerPtr == nullptr) {
        return;
    }

    if (controllerPtr->isNightSleepActive()) {
        return;
    }

    // 1. High-frequency active state (motor running) sampling and buffering
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
                    Serial.printf("[TELEMETRY] Motor stopped. Spawning async active batch transmission of %d points...\n", bufferCount);
                    
                    BatchPayload* payload = new BatchPayload();
                    payload->count = bufferCount;
                    snprintf(payload->type, sizeof(payload->type), "batch");
                    payload->data = new TelemetryData[bufferCount];
                    for (int i = 0; i < bufferCount; i++) {
                        payload->data[i] = eventBuffer[i];
                    }
                    
                    bufferCount = 0;
                    
                    BaseType_t res = xTaskCreate(
                        telemetryTaskFunction,
                        "ActiveBatch",
                        8192,
                        payload,
                        1,
                        NULL
                    );
                    
                    if (res != pdPASS) {
                        Serial.println("[TELEMETRY] Failed to create ActiveBatch task. Reclaiming memory.");
                        delete[] payload->data;
                        delete payload;
                    }
                } else {
                    Serial.println("[TELEMETRY] Motor stopped, but cannot send telemetry (WiFi offline or sleep/unsafe state). Clearing active buffer.");
                    bufferCount = 0;
                }
            }
        }
    }

    // 2. Regular heartbeat sampling (every 1 minute / 60,000ms)
    unsigned long now = millis();
    if (lastHeartbeatSampleTime == 0) {
        lastHeartbeatSampleTime = now;
    }
    
    if (now - lastHeartbeatSampleTime >= 60000) {
        lastHeartbeatSampleTime = now;
        
        TelemetryData data;
        data.battery_v = controllerPtr->getBatteryVoltage();
        data.distance_cm = controllerPtr->getDistance();
        data.motor_current = controllerPtr->getMotorCurrent();
        data.state = (int)controllerPtr->getCurrentState();
        data.timestamp_ms = now;
        
        if (heartbeatCount < 60) {
            heartbeatBuffer[heartbeatCount++] = data;
        } else {
            // Ring buffer shift left
            for (int i = 1; i < 60; i++) {
                heartbeatBuffer[i - 1] = heartbeatBuffer[i];
            }
            heartbeatBuffer[59] = data;
        }
        Serial.printf("[TELEMETRY] Heartbeat sample saved. Buffer count: %d, State: %d\n",
                      heartbeatCount, data.state);
    }

    // 3. Heartbeat batch transmission (every telemetryIntervalMin minutes)
    int intervalMin = controllerPtr->getConfig().telemetryIntervalMin;
    if (intervalMin < 1) intervalMin = 1;
    unsigned long intervalMs = (unsigned long)intervalMin * 60000;

    if (lastSendTime == 0) {
        lastSendTime = now;
    }

    if (now - lastSendTime >= intervalMs) {
        lastSendTime = now;
        
        if (heartbeatCount > 0) {
            if (controllerPtr->canSendTelemetry() && WifiManager::isConnected()) {
                Serial.printf("[TELEMETRY] Heartbeat interval elapsed. Spawning async transmission of %d points...\n", heartbeatCount);
                
                BatchPayload* payload = new BatchPayload();
                payload->count = heartbeatCount;
                snprintf(payload->type, sizeof(payload->type), "heartbeat");
                payload->data = new TelemetryData[heartbeatCount];
                for (int i = 0; i < heartbeatCount; i++) {
                    payload->data[i] = heartbeatBuffer[i];
                }
                
                // Reset heartbeat buffer count
                heartbeatCount = 0;
                
                BaseType_t res = xTaskCreate(
                    telemetryTaskFunction,
                    "HeartbeatBatch",
                    8192,
                    payload,
                    1,
                    NULL
                );
                
                if (res != pdPASS) {
                    Serial.println("[TELEMETRY] Failed to create HeartbeatBatch task. Reclaiming memory.");
                    delete[] payload->data;
                    delete payload;
                }
            } else {
                Serial.println("[TELEMETRY] Heartbeat interval elapsed, but cannot send telemetry. Keeping data in buffer.");
            }
        }
    }
}
