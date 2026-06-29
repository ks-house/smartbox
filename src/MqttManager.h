#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include "RemoteLogger.h"

class SmartBoxController; // Forward declaration
struct TelemetryData; // Forward declaration

class MqttManager;
MqttManager* getMqttManagerInstance();

class MqttManager {
public:
    explicit MqttManager(SmartBoxController& controller);
    ~MqttManager() = default;

    void begin();
    void update(); // Non-blocking state update and timer checks


    void publishLog(LogLevel level, const char* message);
    void publishTelemetry();
    
    // Batch and Event Telemetry Extensions
    void publishBatchTelemetry(const TelemetryData* data, int count, const char* batchType);
    void publishEventState(int prevState, int newState);
    void publishEventMotion(float distanceCm);
    void publishEventAlarm(const char* alarmType, float value, const char* message);
    void publishEventCycle(unsigned long durationMs, float peakCurrent, float startBatt, float endBatt);
    
    // Home Assistant MQTT Auto Discovery
    void publishAutoDiscovery();
    
    bool isConnected() { return m_mqttClient.connected(); }  // BUG-10 fix: removed const_cast anti-pattern
    bool isDebugLoggingActive() const { return m_debugLoggingActive; }

private:
    void connectToMqtt();
    void onMqttConnect(bool sessionPresent);
    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
    void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
    void handleCommand(const JsonDocument& doc);
    void handleSetConfig(const JsonDocument& doc);

    static const char* stateToString(int state);

    SmartBoxController& m_controller;
    AsyncMqttClient m_mqttClient;
    Ticker m_reconnectTimer;
    Ticker m_rebootTimer;

    bool m_debugLoggingActive;
    unsigned long m_debugStartTime;
    bool m_wasConnected;
    bool m_isConnecting;  // BUG-05 fix: prevent duplicate connection attempts

    static constexpr unsigned long DEBUG_MODE_DURATION_MS = 300000; // 5 minutes
};

#endif // MQTT_MANAGER_H
