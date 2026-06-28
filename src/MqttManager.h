#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "SmartBoxController.h"

#include <mutex>

class MqttManager {
public:
    // Initializes the MQTT client and sets the controller reference
    static void init(SmartBoxController& controller);
    
    // Must be called repeatedly in loop() to maintain connection and process incoming messages
    static void update();
    
    // Methods to publish data to the broker
    static void publishState(State newState);
    static void publishTelemetry(const char* jsonPayload);
    static void publishLog(const char* level, const char* message);

private:
    static SmartBoxController* _controller;
    static WiFiClient _wifiClient;
    static PubSubClient _mqttClient;
    static std::mutex _mqttMutex;
    
    static unsigned long _lastReconnectAttempt;
    static const unsigned long RECONNECT_INTERVAL_MS = 5000;
    
    // Callback when a message arrives on a subscribed topic
    static void onMessage(char* topic, byte* payload, unsigned int length);
    
    // Non-blocking reconnect routine
    static void reconnect();
};

#endif // MQTT_MANAGER_H
