#include "TelemetryManager.h"
#include "WifiManager.h"
#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <InfluxDbClient.h>
#include <sys/time.h>

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

// ── InfluxDB log forwarding queue ─────────────────────────
RemoteLogEntry TelemetryManager::logQueue[MAX_LOG_QUEUE];
int TelemetryManager::logQueueCount = 0;
std::mutex TelemetryManager::logQueueMutex;

bool TelemetryManager::wakeupTelemetryPending = false;

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
    
    InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, SECRET_ROOT_CA_CERT);
    
    // Configure write options with Milliseconds write precision
    client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::MS));
    
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
        
        // 1. 센서 측정 거리는 100cm 이하일 때만 수집해서 전송 (data.distance_cm = -1.0f 이면 100cm 초과임)
        if (d.distance_cm >= 0.0f) {
            pointDevice.addField("distance_cm", d.distance_cm);
        }
        
        // 2 & 3. State와 모터 전류는 IDLE 상태가 아닐 때만 수집해서 전송
        if (d.state != (int)STATE_IDLE) {
            pointDevice.addField("motor_current", d.motor_current);
            pointDevice.addField("state", d.state);
            pointDevice.addField("state_str", stateToString((State)d.state));
        }
        
        // 4. WiFi RSSI가 수집되었을 때 전송
        if (d.wifi_rssi != 0) {
            pointDevice.addField("wifi_rssi", d.wifi_rssi);
        }
        
        unsigned long offset = d.timestamp_ms - baseTime;
        pointDevice.addField("offset_ms", (int)offset);
        
        // Calculate the absolute Epoch time in ms for this point
        uint64_t pointEpochMs;
        if (payload->uploadStartMillis >= d.timestamp_ms) {
            pointEpochMs = payload->currentEpochMs - (payload->uploadStartMillis - d.timestamp_ms);
        } else {
            pointEpochMs = payload->currentEpochMs + (d.timestamp_ms - payload->uploadStartMillis);
        }
        pointDevice.setTime(pointEpochMs);
        
        // Append diagnostic data to the first point if there are any failures cached
        if (i == 0 && failedCount > 0) {
            pointDevice.addField("failed_tx_count", failedCount);
            pointDevice.addField("last_error_msg", errMsg.c_str());
            Serial.printf("[TELEMETRY-ASYNC] Appended diagnostics: failed_tx_count=%d, last_error_msg=%s\n",
                          failedCount, errMsg.c_str());
        }
        
        Serial.printf("[TELEMETRY-ASYNC] Batch point %d/%d -> offset: %d ms, timestamp: %llu, batt: %.2f V, curr: %.1f mA, state: %s\n",
                      i + 1, payload->count, (int)offset, pointEpochMs, d.battery_v, d.motor_current, stateToString((State)d.state));
                      
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

    // ── Flush InfluxDB log queue (WARN/ERROR entries) ─────
    {
        std::lock_guard<std::mutex> lock(logQueueMutex);
        if (logQueueCount > 0) {
            Serial.printf("[TELEMETRY-ASYNC] Flushing %d log entries to InfluxDB (smartbox_log)...\n", logQueueCount);

            struct timeval tvLog;
            gettimeofday(&tvLog, NULL);
            uint64_t nowEpochMs = (uint64_t)tvLog.tv_sec * 1000 + (tvLog.tv_usec / 1000);
            unsigned long nowMs = millis();

            for (int i = 0; i < logQueueCount; i++) {
                const RemoteLogEntry& le = logQueue[i];
                Point logPoint("smartbox_log");
                logPoint.addTag("device", DEVICE_TAG);
                const char* lvlStr = "INFO";
                switch (le.level) {
                    case LogLevel::DEBUG: lvlStr = "DEBUG"; break;
                    case LogLevel::INFO:  lvlStr = "INFO";  break;
                    case LogLevel::WARN:  lvlStr = "WARN";  break;
                    case LogLevel::ERROR: lvlStr = "ERROR"; break;
                }
                logPoint.addTag("level", lvlStr);
                logPoint.addField("message", le.message);

                // Approximate epoch time for this log entry
                uint64_t logEpochMs;
                if (nowMs >= le.timestamp_ms) {
                    logEpochMs = nowEpochMs - (nowMs - le.timestamp_ms);
                } else {
                    logEpochMs = nowEpochMs;
                }
                logPoint.setTime(logEpochMs);

                if (!client.writePoint(logPoint)) {
                    Serial.printf("[TELEMETRY-ASYNC] Log entry %d write failed: %s\n",
                                  i, client.getLastErrorMessage().c_str());
                }
            }
            logQueueCount = 0;
            Serial.println("[TELEMETRY-ASYNC] Log queue flushed.");
        }
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
    logQueueCount = 0;
    wakeupTelemetryPending = false;
    
    std::lock_guard<std::mutex> lock(diagMutex);
    pendingFailedTxCount = 0;
    lastNetworkError = "";
}

void TelemetryManager::notifySleepEnd() {
    wakeupTelemetryPending = true;
    lastSendTime = 0; // Force immediate heartbeat on next update cycle
}

// ── pushLog: called by RemoteLogger to queue WARN/ERROR for InfluxDB ─
void TelemetryManager::pushLog(LogLevel level, const char* message) {
    std::lock_guard<std::mutex> lock(logQueueMutex);
    if (logQueueCount >= MAX_LOG_QUEUE) {
        // Ring: drop oldest entry
        for (int i = 1; i < MAX_LOG_QUEUE; i++) {
            logQueue[i - 1] = logQueue[i];
        }
        logQueueCount = MAX_LOG_QUEUE - 1;
    }
    RemoteLogEntry& e = logQueue[logQueueCount++];
    e.timestamp_ms = millis();
    e.level = level;
    strncpy(e.message, message, sizeof(e.message) - 1);
    e.message[sizeof(e.message) - 1] = '\0';
}

void TelemetryManager::update() {
    if (controllerPtr == nullptr) {
        return;
    }

    if (controllerPtr->isNightSleepActive()) {
        return;
    }

    unsigned long now = millis();

    // 0. Wakeup Telemetry processing
    if (wakeupTelemetryPending && WifiManager::isConnected()) {
        wakeupTelemetryPending = false;
        if (controllerPtr->canSendTelemetry()) {
            Serial.println("[TELEMETRY] Spawning async wakeup transmission after sleep...");
            BatchPayload* payload = new BatchPayload();
            payload->count = 1;
            snprintf(payload->type, sizeof(payload->type), "wakeup");
            payload->data = new TelemetryData[1];
            
            TelemetryData d;
            d.battery_v = controllerPtr->getBatteryVoltage();
            float dist = controllerPtr->getDistance();
            d.distance_cm = (dist <= 100.0f) ? dist : -1.0f;
            d.motor_current = controllerPtr->getMotorCurrent();
            d.state = (int)controllerPtr->getCurrentState();
            d.wifi_rssi = WiFi.RSSI();
            d.timestamp_ms = now;
            
            payload->data[0] = d;
            payload->uploadStartMillis = now;
            struct timeval tv;
            gettimeofday(&tv, NULL);
            payload->currentEpochMs = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
            
            BaseType_t res = xTaskCreate(
                telemetryTaskFunction,
                "WakeupBatch",
                8192,
                payload,
                1,
                NULL
            );
            if (res != pdPASS) {
                delete[] payload->data;
                delete payload;
            }
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
                    
                    payload->uploadStartMillis = millis();
                    struct timeval tv;
                    gettimeofday(&tv, NULL);
                    payload->currentEpochMs = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
                    
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
                
                payload->uploadStartMillis = millis();
                struct timeval tv;
                gettimeofday(&tv, NULL);
                payload->currentEpochMs = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
                
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

    // 4. WiFi change detection (every 10 seconds)
    static bool lastWifiConnected = false;
    static int lastWifiRssi = 0;
    static unsigned long lastWifiCheckTime = 0;
    
    if (now - lastWifiCheckTime >= 10000) {
        lastWifiCheckTime = now;
        
        bool currentWifiConnected = WifiManager::isConnected();
        int currentWifiRssi = currentWifiConnected ? WiFi.RSSI() : 0;
        
        bool statusChanged = (currentWifiConnected != lastWifiConnected);
        bool rssiChanged = currentWifiConnected && (abs(currentWifiRssi - lastWifiRssi) >= 5);
        
        if (statusChanged || rssiChanged) {
            Serial.printf("[TELEMETRY] WiFi change detected. Connected: %d -> %d, RSSI: %d -> %d\n",
                          lastWifiConnected, currentWifiConnected, lastWifiRssi, currentWifiRssi);
            
            lastWifiConnected = currentWifiConnected;
            lastWifiRssi = currentWifiRssi;
            
            if (currentWifiConnected && controllerPtr->canSendTelemetry()) {
                Serial.println("[TELEMETRY] Spawning async WiFi change transmission...");
                
                BatchPayload* payload = new BatchPayload();
                payload->count = 1;
                snprintf(payload->type, sizeof(payload->type), "wifi");
                payload->data = new TelemetryData[1];
                
                TelemetryData d;
                d.battery_v = controllerPtr->getBatteryVoltage();
                float dist = controllerPtr->getDistance();
                d.distance_cm = (dist <= 100.0f) ? dist : -1.0f;
                d.motor_current = controllerPtr->getMotorCurrent();
                d.state = (int)controllerPtr->getCurrentState();
                d.wifi_rssi = currentWifiRssi;
                d.timestamp_ms = now;
                
                payload->data[0] = d;
                payload->uploadStartMillis = now;
                struct timeval tv;
                gettimeofday(&tv, NULL);
                payload->currentEpochMs = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
                
                BaseType_t res = xTaskCreate(
                    telemetryTaskFunction,
                    "WifiBatch",
                    8192,
                    payload,
                    1,
                    NULL
                );
                if (res != pdPASS) {
                    Serial.println("[TELEMETRY] Failed to create WifiBatch task. Reclaiming memory.");
                    delete[] payload->data;
                    delete payload;
                }
            }
        }
    }
}
