#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include "RemoteLogger.h"

class SmartBoxController; // Forward declaration

class MqttManager {
public:
    explicit MqttManager(SmartBoxController& controller);
    ~MqttManager() = default;

    void begin();
    void update(); // Non-blocking state update and timer checks

    void onNightSleepStart();
    void onNightSleepEnd();

    void publishLog(LogLevel level, const char* message);
    void publishTelemetry();
    
    bool isConnected() const { return const_cast<AsyncMqttClient&>(m_mqttClient).connected(); }
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
    bool m_wasNightSleep;
    bool m_wasConnected;

    static constexpr unsigned long DEBUG_MODE_DURATION_MS = 300000; // 5 minutes
};

#endif // MQTT_MANAGER_H
