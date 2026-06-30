#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <PsychicMqttClient.h>
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
    
    bool isConnected() { return m_mqttClient.connected(); }
    bool isDebugLoggingActive() const { return m_debugLoggingActive; }

private:
    void connectToMqtt();
    void onMqttConnect(bool sessionPresent);
    void onMqttDisconnect();
    void onMqttMessage(char* topic, char* payload, int retain, int qos, bool dup);
    void handleCommand(const JsonDocument& doc);
    void handleSetConfig(const JsonDocument& doc);

    static const char* stateToString(int state);

    SmartBoxController& m_controller;
    PsychicMqttClient m_mqttClient;
    Ticker m_reconnectTimer;
    Ticker m_rebootTimer;

    bool m_debugLoggingActive;
    unsigned long m_debugStartTime;
    bool m_wasConnected;
    bool m_isConnecting;
    unsigned long m_connectStartTime;
    String m_serverUri;

    static constexpr unsigned long DEBUG_MODE_DURATION_MS = 300000; // 5 minutes
};

#endif // MQTT_MANAGER_H
