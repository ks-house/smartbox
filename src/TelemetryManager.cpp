#include "TelemetryManager.h"
#include "MqttManager.h"
#include "WifiManager.h"
#include <Arduino.h>
#include <WiFi.h>

SmartBoxController* TelemetryManager::controllerPtr = nullptr;
unsigned long TelemetryManager::lastSendTime = 0;
TelemetryData TelemetryManager::eventBuffer[MAX_BATCH_SIZE];
int TelemetryManager::bufferCount = 0;
bool TelemetryManager::wasMotorRunning = false;
unsigned long TelemetryManager::lastSampleTime = 0;

TelemetryData TelemetryManager::heartbeatBuffer[60];
int TelemetryManager::heartbeatCount = 0;
unsigned long TelemetryManager::lastHeartbeatSampleTime = 0;

bool TelemetryManager::wakeupTelemetryPending = false;

void TelemetryManager::dispatchMqttBatch(const TelemetryData* data, int count, const char* batchType) {
    if (data == nullptr || count <= 0) return;
    MqttManager* mqtt = getMqttManagerInstance();
    if (mqtt) {
        Serial.printf("[TELEMETRY] Dispatching %s batch telemetry (%d points) to MQTT...\n", batchType, count);
        mqtt->publishBatchTelemetry(data, count, batchType);
    }
}

void TelemetryManager::init(SmartBoxController& controller) {
    controllerPtr = &controller;
    lastSendTime = 0;
    bufferCount = 0;
    wasMotorRunning = false;
    lastSampleTime = 0;
    heartbeatCount = 0;
    lastHeartbeatSampleTime = 0;
    wakeupTelemetryPending = false;
}

void TelemetryManager::notifySleepEnd() {
    wakeupTelemetryPending = true;
    lastSendTime = 0; // Force immediate heartbeat on next update cycle
}

void TelemetryManager::update() {
    if (controllerPtr == nullptr || controllerPtr->isNightSleepActive()) {
        return;
    }

    unsigned long now = millis();

    // 0. Wakeup Telemetry processing
    if (wakeupTelemetryPending && WifiManager::isConnected()) {
        wakeupTelemetryPending = false;
        if (controllerPtr->canSendTelemetry()) {
            TelemetryData d;
            d.battery_v = controllerPtr->getBatteryVoltage();
            float dist = controllerPtr->getDistance();
            d.distance_cm = (dist <= 100.0f) ? dist : -1.0f;
            d.motor_current = controllerPtr->getMotorCurrent();
            d.state = (int)controllerPtr->getCurrentState();
            d.wifi_rssi = WiFi.RSSI();
            d.timestamp_ms = now;
            
            dispatchMqttBatch(&d, 1, "wakeup");
        }
    }

    // 1. High-frequency active state (motor running) sampling and buffering
    bool isMotor = controllerPtr->isMotorRunning();

    if (isMotor) {
        wasMotorRunning = true;
        if (now - lastSampleTime >= 500) {
            lastSampleTime = now;
            
            TelemetryData data;
            data.battery_v = controllerPtr->getBatteryVoltage();
            float dist = controllerPtr->getDistance();
            data.distance_cm = (dist <= 100.0f) ? dist : -1.0f;
            data.motor_current = controllerPtr->getMotorCurrent();
            data.state = (int)controllerPtr->getCurrentState();
            data.wifi_rssi = WifiManager::isConnected() ? WiFi.RSSI() : 0;
            data.timestamp_ms = now;
            
            if (bufferCount < MAX_BATCH_SIZE) {
                eventBuffer[bufferCount++] = data;
            } else {
                for (int i = 1; i < MAX_BATCH_SIZE; i++) {
                    eventBuffer[i - 1] = eventBuffer[i];
                }
                eventBuffer[MAX_BATCH_SIZE - 1] = data;
            }
        }
    } else {
        // Motor is NOT running. Check if it was running previously
        if (wasMotorRunning) {
            wasMotorRunning = false;
            if (bufferCount > 0) {
                if (controllerPtr->canSendTelemetry() && WifiManager::isConnected()) {
                    dispatchMqttBatch(eventBuffer, bufferCount, "batch");
                }
                bufferCount = 0;
            }
        }
    }

    // 2. Regular heartbeat sampling (every 1 minute / 60,000ms)
    if (lastHeartbeatSampleTime == 0) {
        lastHeartbeatSampleTime = now;
    }
    
    if (now - lastHeartbeatSampleTime >= 60000) {
        lastHeartbeatSampleTime = now;
        
        TelemetryData data;
        data.battery_v = controllerPtr->getBatteryVoltage();
        float dist = controllerPtr->getDistance();
        data.distance_cm = (dist <= 100.0f) ? dist : -1.0f;
        data.motor_current = controllerPtr->getMotorCurrent();
        data.state = (int)controllerPtr->getCurrentState();
        data.wifi_rssi = WifiManager::isConnected() ? WiFi.RSSI() : 0;
        data.timestamp_ms = now;
        
        if (heartbeatCount < 60) {
            heartbeatBuffer[heartbeatCount++] = data;
        } else {
            for (int i = 1; i < 60; i++) {
                heartbeatBuffer[i - 1] = heartbeatBuffer[i];
            }
            heartbeatBuffer[59] = data;
        }
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
                dispatchMqttBatch(heartbeatBuffer, heartbeatCount, "heartbeat");
                heartbeatCount = 0;
            }
        }
    }
}
