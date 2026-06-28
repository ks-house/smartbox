#include "MqttManager.h"
#include "secrets.h"

// Topics
static const char* TOPIC_COMMAND   = "smartbox/command";
static const char* TOPIC_STATE     = "smartbox/state";
static const char* TOPIC_TELEMETRY = "smartbox/telemetry";
static const char* TOPIC_LOG       = "smartbox/log";

SmartBoxController* MqttManager::_controller = nullptr;
WiFiClient MqttManager::_wifiClient;
PubSubClient MqttManager::_mqttClient(_wifiClient);
std::mutex MqttManager::_mqttMutex;
unsigned long MqttManager::_lastReconnectAttempt = 0;

void MqttManager::init(SmartBoxController& controller) {
    _controller = &controller;
    
    _mqttClient.setServer(SECRET_MQTT_HOST, SECRET_MQTT_PORT);
    _mqttClient.setCallback(MqttManager::onMessage);
    
    _lastReconnectAttempt = 0;
    Serial.println("[MQTT] Manager initialized");
}

void MqttManager::update() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    if (!_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > RECONNECT_INTERVAL_MS) {
            _lastReconnectAttempt = now;
            std::lock_guard<std::mutex> lock(_mqttMutex);
            reconnect();
        }
    } else {
        std::lock_guard<std::mutex> lock(_mqttMutex);
        _mqttClient.loop();
    }
}

void MqttManager::reconnect() {
    Serial.print("[MQTT] Attempting connection to ");
    Serial.print(SECRET_MQTT_HOST);
    Serial.print("...");
    
    // Create a random client ID
    String clientId = "SmartBox-";
    clientId += String(random(0xffff), HEX);
    
    if (_mqttClient.connect(clientId.c_str(), SECRET_MQTT_USER, SECRET_MQTT_PASS)) {
        Serial.println(" connected");
        
        // Once connected, subscribe to the command topic
        _mqttClient.subscribe(TOPIC_COMMAND);
        
        // Publish current state
        if (_controller) {
            publishState(_controller->getCurrentState());
        }
    } else {
        Serial.print(" failed, rc=");
        Serial.print(_mqttClient.state());
        Serial.println(" try again in 5 seconds");
    }
}

void MqttManager::onMessage(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (unsigned int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }
    msg.trim();
    msg.toUpperCase();
    
    Serial.printf("[MQTT] Message arrived on topic: %s, payload: %s\n", topic, msg.c_str());
    
    if (String(topic) == TOPIC_COMMAND && _controller != nullptr) {
        if (msg == "OPEN") {
            _controller->forceOpen();
        } else if (msg == "CLOSE") {
            _controller->forceClose();
        }
    }
}

void MqttManager::publishState(State newState) {
    std::lock_guard<std::mutex> lock(_mqttMutex);
    if (_mqttClient.connected()) {
        const char* stateStr = "UNKNOWN";
        switch (newState) {
            case STATE_IDLE: stateStr = "IDLE"; break;
            case STATE_OPEN_START: stateStr = "OPEN_START"; break;
            case STATE_OPENING: stateStr = "OPENING"; break;
            case STATE_HOLD: stateStr = "HOLD"; break;
            case STATE_CLOSE_START: stateStr = "CLOSE_START"; break;
            case STATE_CLOSING: stateStr = "CLOSING"; break;
            case STATE_EMERGENCY_STOP: stateStr = "EMERGENCY_STOP"; break;
            case STATE_BATTERY_LOW_SHUTDOWN: stateStr = "BATTERY_LOW"; break;
            case STATE_STARTUP_OPEN: stateStr = "STARTUP_OPEN"; break;
            case STATE_OTA_UPDATING: stateStr = "OTA_UPDATING"; break;
            case STATE_MAINTENANCE: stateStr = "MAINTENANCE"; break;
        }
        _mqttClient.publish(TOPIC_STATE, stateStr);
    }
}

void MqttManager::publishTelemetry(const char* jsonPayload) {
    std::lock_guard<std::mutex> lock(_mqttMutex);
    if (_mqttClient.connected()) {
        _mqttClient.publish(TOPIC_TELEMETRY, jsonPayload);
    }
}

void MqttManager::publishLog(const char* level, const char* message) {
    std::lock_guard<std::mutex> lock(_mqttMutex);
    if (_mqttClient.connected()) {
        String payload = String("[") + level + "] " + message;
        _mqttClient.publish(TOPIC_LOG, payload.c_str());
    }
}
