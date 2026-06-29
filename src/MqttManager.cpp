#include "MqttManager.h"
#include "SmartBoxController.h"
#include "WifiManager.h"
#include "AutoOtaManager.h"
#include "ConfigManager.h"
#include "TelemetryManager.h"
#include "secrets.h"
#include <WiFi.h>

static MqttManager* g_mqttManagerInstance = nullptr;

MqttManager* getMqttManagerInstance() {
    return g_mqttManagerInstance;
}

static void globalLogForwardCallback(LogLevel level, const char* message) {
    if (g_mqttManagerInstance) {
        g_mqttManagerInstance->publishLog(level, message);
    }
}

MqttManager::MqttManager(SmartBoxController& controller)
    : m_controller(controller),
      m_debugLoggingActive(false),
      m_debugStartTime(0),
      m_wasConnected(false),
      m_isConnecting(false),  // BUG-05 fix
      m_connectStartTime(0) {
    g_mqttManagerInstance = this;
}

void MqttManager::begin() {
    m_mqttClient.setServer(SECRET_MQTT_HOST, SECRET_MQTT_PORT);
    m_mqttClient.setCredentials(SECRET_MQTT_USER, SECRET_MQTT_PASS);
    
#if ASYNC_TCP_SSL_ENABLED
    m_mqttClient.setSecure(true);
#endif

    // Set Will (Last Will and Testament) message
    static const char* willPayload = "{\"status\":\"offline\",\"device_id\":\"ESP32C6_SMARTBOX_01\"}";
    m_mqttClient.setWill("smartbox/status", 1, true, willPayload, strlen(willPayload));

    // Register AsyncMqttClient callbacks
    m_mqttClient.onConnect([this](bool sessionPresent) {
        this->onMqttConnect(sessionPresent);
    });

    m_mqttClient.onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
        this->onMqttDisconnect(reason);
    });

    m_mqttClient.onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
        this->onMqttMessage(topic, payload, properties, len, index, total);
    });

    // Register callback into RemoteLogger for log forwarding
    RemoteLogger::onWarnError = globalLogForwardCallback;

    RLOG_I("[MQTT] Manager initialized for %s:%d\n", SECRET_MQTT_HOST, SECRET_MQTT_PORT);
}

void MqttManager::connectToMqtt() {
    // BUG-05 fix: prevent duplicate connection attempts
    if (m_isConnecting) {
        RLOG_D("[MQTT] Connection already in progress, skipping.\n");
        return;
    }

    if (WifiManager::isConnected() && !m_mqttClient.connected()) {
        RLOG_I("[MQTT] Connecting to MQTT broker...\n");
        m_isConnecting = true;
        m_connectStartTime = millis();
        m_mqttClient.connect();
    }
}

void MqttManager::onMqttConnect(bool sessionPresent) {
    RLOG_I("[MQTT] Connected to broker successfully! Session present: %d\n", sessionPresent);
    m_reconnectTimer.detach();
    m_wasConnected = true;
    m_isConnecting = false;  // BUG-05 fix

    // Subscribe to command topic
    m_mqttClient.subscribe("smartbox/command", 1);
    RLOG_I("[MQTT] Subscribed to smartbox/command\n");

    // Publish Online status
    JsonDocument doc;
    doc["status"] = "online";
    doc["device_id"] = "ESP32C6_SMARTBOX_01";
    doc["firmware_version"] = m_controller.getFirmwareVersion();
    doc["ip"] = WiFi.localIP().toString();

    String output;
    serializeJson(doc, output);
    m_mqttClient.publish("smartbox/status", 1, true, output.c_str());

    // Publish Home Assistant MQTT Auto Discovery Configs
    publishAutoDiscovery();
}

void MqttManager::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    RLOG_W("[MQTT] Disconnected from MQTT broker. Reason: %d\n", static_cast<int>(reason));
    m_wasConnected = false;
    m_isConnecting = false;  // BUG-05 fix: clear connecting flag on disconnect

    if (WifiManager::isConnected()) {
        m_reconnectTimer.once(5.0f, +[](MqttManager* instance) {
            instance->connectToMqtt();
        }, this);
    }
}

void MqttManager::onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
    char message[len + 1];
    memcpy(message, payload, len);
    message[len] = '\0';

    RLOG_I("[MQTT] Message arrived [%s]: %s\n", topic, message);

    if (strcmp(topic, "smartbox/command") == 0) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, message);
        if (error) {
            RLOG_E("[MQTT] JSON parse failed: %s\n", error.c_str());
            return;
        }
        handleCommand(doc);
    }
}

void MqttManager::handleCommand(const JsonDocument& doc) {
    if (doc["command"].isNull() || !doc["command"].is<const char*>()) {
        RLOG_W("[MQTT] Command missing or invalid string format\n");
        publishLog(LogLevel::WARN, "[MQTT] Invalid command format received");
        return;
    }

    const char* cmd = doc["command"];
    RLOG_I("[MQTT] Processing command: %s\n", cmd);

    if (strcmp(cmd, "force_open") == 0) {
        m_controller.forceOpen();
        publishLog(LogLevel::INFO, "[MQTT] Executed command: force_open");
    } else if (strcmp(cmd, "force_close") == 0) {
        m_controller.forceClose();
        publishLog(LogLevel::INFO, "[MQTT] Executed command: force_close");
    } else if (strcmp(cmd, "reboot") == 0) {
        RLOG_I("[MQTT] Reboot requested. Scheduling restart in 1000ms...\n");
        publishLog(LogLevel::INFO, "[MQTT] Rebooting ESP32-C6 in 1 second...");
        m_rebootTimer.once(1.0f, +[]() {
            ESP.restart();
        });
    } else if (strcmp(cmd, "trigger_ota") == 0) {
        RLOG_I("[MQTT] Manual OTA Update triggered via MQTT\n");
        if (AutoOtaManager::startOtaUpdate(true)) {
            publishLog(LogLevel::INFO, "[MQTT] NAS HTTPS OTA update initiated successfully");
        } else {
            publishLog(LogLevel::ERROR, "[MQTT] Failed to initiate NAS HTTPS OTA update");
        }
    } else if (strcmp(cmd, "reset_emergency") == 0 || strcmp(cmd, "release_emergency") == 0) {
        m_controller.resetEmergency();
        publishLog(LogLevel::INFO, "[MQTT] Emergency stop lock released");
    } else if (strcmp(cmd, "maintenance") == 0) {
        const char* action = doc["action"] | "";
        if (strcmp(action, "start") == 0) {
            m_controller.startMaintenanceMode();
            publishLog(LogLevel::INFO, "[MQTT] Maintenance mode started");
        } else if (strcmp(action, "stop") == 0) {
            m_controller.stopMaintenanceMode();
            publishLog(LogLevel::INFO, "[MQTT] Maintenance mode stopped");
        } else {
            publishLog(LogLevel::WARN, "[MQTT] Invalid maintenance action");
        }
    } else if (strcmp(cmd, "debug") == 0) {
        const char* val = doc["value"] | "off";
        if (strcmp(val, "on") == 0) {
            m_debugLoggingActive = true;
            m_debugStartTime = millis();
            RLOG_I("[MQTT] Dynamic Debug Logging ENABLED for 5 minutes\n");
            publishLog(LogLevel::INFO, "[MQTT] Dynamic Debug Logging ENABLED for 5 minutes");
        } else {
            m_debugLoggingActive = false;
            RLOG_I("[MQTT] Dynamic Debug Logging DISABLED\n");
            publishLog(LogLevel::INFO, "[MQTT] Dynamic Debug Logging DISABLED");
        }
    } else if (strcmp(cmd, "set_config") == 0) {
        handleSetConfig(doc);
    } else {
        RLOG_W("[MQTT] Unknown command received: %s\n", cmd);
        char errBuf[64];
        snprintf(errBuf, sizeof(errBuf), "[MQTT] Unknown command: %s", cmd);
        publishLog(LogLevel::WARN, errBuf);
    }
}

void MqttManager::handleSetConfig(const JsonDocument& doc) {
    BoxConfig cfg = m_controller.getConfig();
    bool updated = false;
    char logBuf[128];

    // Single key-value pair update format: {"command": "set_config", "key": "stall_current", "value": 3500}
    if (!doc["key"].isNull() && !doc["value"].isNull()) {
        const char* key = doc["key"];
        if (strcmp(key, "stall") == 0 || strcmp(key, "stall_current") == 0 || strcmp(key, "currentStallLimit") == 0) {
            float val = doc["value"].as<float>();
            float clamped = constrain(val, 500.0f, 10000.0f);
            cfg.currentStallLimit = clamped;
            updated = true;
            snprintf(logBuf, sizeof(logBuf), "[MQTT Config] stall_current updated to %.1f mA (clamped)", clamped);
        } else if (strcmp(key, "wait") == 0 || strcmp(key, "hold_time_ms") == 0 || strcmp(key, "waitTime") == 0) {
            unsigned long val = doc["value"].as<unsigned long>();
            unsigned long clamped = constrain(val, 1000UL, 60000UL);
            cfg.waitTime = clamped;
            updated = true;
            snprintf(logBuf, sizeof(logBuf), "[MQTT Config] hold_time_ms updated to %lu ms (clamped)", clamped);
        } else if (strcmp(key, "dist") == 0 || strcmp(key, "dist_threshold") == 0 || strcmp(key, "distThreshold") == 0) {
            float val = doc["value"].as<float>();
            float clamped = constrain(val, 5.0f, 150.0f);
            cfg.distThreshold = clamped;
            updated = true;
            snprintf(logBuf, sizeof(logBuf), "[MQTT Config] dist_threshold updated to %.1f cm (clamped)", clamped);
        } else if (strcmp(key, "otaHour") == 0 || strcmp(key, "ota_hour") == 0) {
            int val = doc["value"].as<int>();
            int clamped = constrain(val, 0, 23);
            cfg.otaHour = clamped;
            updated = true;
            snprintf(logBuf, sizeof(logBuf), "[MQTT Config] ota_hour updated to %d (clamped)", clamped);
        } else if (strcmp(key, "telInterval") == 0 || strcmp(key, "telemetry_interval") == 0) {
            int val = doc["value"].as<int>();
            int clamped = constrain(val, 1, 1440);
            cfg.telemetryIntervalMin = clamped;
            updated = true;
            snprintf(logBuf, sizeof(logBuf), "[MQTT Config] telemetry_interval updated to %d min (clamped)", clamped);
        } else {
            snprintf(logBuf, sizeof(logBuf), "[MQTT Config] Unknown config key: %s", key);
            publishLog(LogLevel::WARN, logBuf);
            return;
        }
    } 
    // Object format: {"command": "set_config", "config": {"stall": 3000, "wait": 10000}}
    else if (doc["config"].is<JsonObjectConst>()) {
        JsonObjectConst cObj = doc["config"].as<JsonObjectConst>();
        if (!cObj["stall"].isNull()) cfg.currentStallLimit = constrain(cObj["stall"].as<float>(), 500.0f, 10000.0f);
        if (!cObj["wait"].isNull()) cfg.waitTime = constrain(cObj["wait"].as<unsigned long>(), 1000UL, 60000UL);
        if (!cObj["dist"].isNull()) cfg.distThreshold = constrain(cObj["dist"].as<float>(), 5.0f, 150.0f);
        if (!cObj["otaHour"].isNull()) cfg.otaHour = constrain(cObj["otaHour"].as<int>(), 0, 23);
        if (!cObj["telInterval"].isNull()) cfg.telemetryIntervalMin = constrain(cObj["telInterval"].as<int>(), 1, 1440);
        updated = true;
        snprintf(logBuf, sizeof(logBuf), "[MQTT Config] Multiple config parameters updated successfully with clamping");
    } else {
        publishLog(LogLevel::WARN, "[MQTT Config] Missing key/value or config object");
        return;
    }

    if (updated) {
        m_controller.setConfig(cfg);
        ConfigManager::saveConfig(cfg);
        RLOG_I("%s\n", logBuf);
        publishLog(LogLevel::INFO, logBuf);
    }
}

void MqttManager::update() {
    // BUG-05 fix: connection timeout guard (15 seconds)
    if (m_isConnecting && (millis() - m_connectStartTime > 15000)) {
        RLOG_W("[MQTT] Connection attempt timed out (15s). Resetting connecting flag.\n");
        m_isConnecting = false;
        if (m_mqttClient.connected()) {
            m_mqttClient.disconnect();
        }
    }

    // BUG-05 fix: only attempt fallback connect if not already connecting/connected
    if (WifiManager::isConnected() && !m_mqttClient.connected() && !m_isConnecting) {
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 10000) {
            lastCheck = millis();
            connectToMqtt();
        }
    }

    // Dynamic logging timer check (5 minutes duration)
    if (m_debugLoggingActive) {
        if (millis() - m_debugStartTime >= DEBUG_MODE_DURATION_MS) {
            m_debugLoggingActive = false;
            RLOG_I("[MQTT] Dynamic Debug Logging timed out after 5 minutes. Reverting to Normal mode.\n");
            publishLog(LogLevel::INFO, "[MQTT] Dynamic Debug Logging timed out after 5 minutes. Reverting to Normal mode.");
        }
    }
}

void MqttManager::publishLog(LogLevel level, const char* message) {
    if (!m_mqttClient.connected()) return;

    // Normal mode: Publish WARN and ERROR/FATAL only. Debug mode: Publish all logs.
    if (!m_debugLoggingActive && (level < LogLevel::WARN)) {
        return;
    }

    const char* levelStr = "INFO";
    switch (level) {
        case LogLevel::DEBUG: levelStr = "DEBUG"; break;
        case LogLevel::INFO:  levelStr = "INFO";  break;
        case LogLevel::WARN:  levelStr = "WARN";  break;
        case LogLevel::ERROR: levelStr = "ERROR"; break;
    }

    JsonDocument doc;
    doc["level"] = levelStr;
    doc["message"] = message;
    doc["timestamp"] = millis();

    String output;
    serializeJson(doc, output);
    m_mqttClient.publish("smartbox/log", 0, false, output.c_str());
}

const char* MqttManager::stateToString(int state) {
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

void MqttManager::publishTelemetry() {
    if (!m_mqttClient.connected()) return;

    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["fsm_state"] = stateToString(static_cast<int>(m_controller.getCurrentState()));
    doc["distance_cm"] = m_controller.getDistance();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["free_heap"] = ESP.getFreeHeap();

    String output;
    serializeJson(doc, output);
    m_mqttClient.publish("smartbox/telemetry", 0, false, output.c_str());
}

void MqttManager::publishBatchTelemetry(const TelemetryData* data, int count, const char* batchType) {
    if (!m_mqttClient.connected() || data == nullptr || count <= 0) return;

    JsonDocument doc;
    doc["device"] = "smartbox_01";
    doc["type"] = batchType;
    doc["count"] = count;
    doc["timestamp"] = millis();
    doc["wifi_rssi"] = WiFi.RSSI();

    JsonArray array = doc["data"].to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject obj = array.add<JsonObject>();
        obj["ts"] = data[i].timestamp_ms;
        obj["batt_v"] = data[i].battery_v;
        if (data[i].distance_cm >= 0.0f) {
            obj["dist_cm"] = data[i].distance_cm;
        }
        if (data[i].state != 0) { // NON-IDLE
            obj["curr_ma"] = data[i].motor_current;
            obj["state"] = stateToString(data[i].state);
        }
    }

    String output;
    serializeJson(doc, output);
    m_mqttClient.publish("smartbox/telemetry/batch", 0, false, output.c_str());
}

void MqttManager::publishEventState(int prevState, int newState) {
    if (!m_mqttClient.connected()) return;

    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["prev_state"] = stateToString(prevState);
    doc["new_state"] = stateToString(newState);
    doc["battery_v"] = m_controller.getBatteryVoltage();

    String output;
    serializeJson(doc, output);
    m_mqttClient.publish("smartbox/event/state", 1, false, output.c_str());
}

void MqttManager::publishEventMotion(float distanceCm) {
    if (!m_mqttClient.connected()) return;

    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["event"] = "motion_detected";
    doc["distance_cm"] = distanceCm;

    String output;
    serializeJson(doc, output);
    m_mqttClient.publish("smartbox/event/motion", 0, false, output.c_str());
}

void MqttManager::publishEventAlarm(const char* alarmType, float value, const char* message) {
    if (!m_mqttClient.connected()) return;

    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["alarm"] = alarmType;
    doc["value"] = value;
    doc["message"] = message;
    doc["battery_v"] = m_controller.getBatteryVoltage();

    String output;
    serializeJson(doc, output);
    m_mqttClient.publish("smartbox/event/alarm", 1, false, output.c_str());
}

void MqttManager::publishEventCycle(unsigned long durationMs, float peakCurrent, float startBatt, float endBatt) {
    if (!m_mqttClient.connected()) return;

    JsonDocument doc;
    doc["timestamp"] = millis();
    doc["duration_ms"] = durationMs;
    doc["peak_current_ma"] = peakCurrent;
    doc["start_batt_v"] = startBatt;
    doc["end_batt_v"] = endBatt;

    String output;
    serializeJson(doc, output);
    m_mqttClient.publish("smartbox/event/cycle", 0, false, output.c_str());
}

void MqttManager::publishAutoDiscovery() {
    if (!m_mqttClient.connected()) return;

    RLOG_I("[MQTT] Publishing Home Assistant Auto-Discovery configs...\n");

    auto createDiscoveryDoc = [&](const char* name, const char* objectId, const char* component) -> JsonDocument {
        JsonDocument doc;
        doc["name"] = name;
        doc["unique_id"] = String("smartbox_01_") + objectId;
        
        JsonObject device = doc["device"].to<JsonObject>();
        JsonArray ids = device["identifiers"].to<JsonArray>();
        ids.add("smartbox_01");
        device["name"] = "SmartBox";
        device["model"] = "SmartBox ESP32-C6 Controller";
        device["manufacturer"] = "KS-House";
        device["sw_version"] = m_controller.getFirmwareVersion();
        
        return doc;
    };

    auto pubConfig = [&](const char* component, const char* objectId, JsonDocument& doc) {
        String topic = String("homeassistant/") + component + "/smartbox_01/" + objectId + "/config";
        String payload;
        serializeJson(doc, payload);
        m_mqttClient.publish(topic.c_str(), 1, true, payload.c_str());
    };

    // 1. Buttons
    struct ButtonDef { const char* id; const char* name; const char* cmd; const char* icon; };
    ButtonDef buttons[] = {
        {"force_open", "[SmartBox] 수거함 강제 열기", "{\"command\": \"force_open\"}", "mdi:trash-can-open"},
        {"force_close", "[SmartBox] 수거함 강제 닫기", "{\"command\": \"force_close\"}", "mdi:trash-can"},
        {"reboot", "[SmartBox] 재부팅", "{\"command\": \"reboot\"}", "mdi:restart"},
        {"trigger_ota", "[SmartBox] 펌웨어 강제 업데이트 (OTA)", "{\"command\": \"trigger_ota\"}", "mdi:cloud-download"},
        {"debug_on", "[SmartBox] 디버그 로깅 5분 활성화", "{\"command\": \"debug\", \"value\": \"on\"}", "mdi:bug-play"},
        {"maint_start", "[SmartBox] 정비 모드 시작 (5분)", "{\"command\": \"maintenance\", \"action\": \"start\"}", "mdi:wrench"},
        {"reset_emergency", "[SmartBox] 비상 정지 락 해제", "{\"command\": \"reset_emergency\"}", "mdi:lock-reset"}
    };
    for (const auto& b : buttons) {
        JsonDocument doc = createDiscoveryDoc(b.name, b.id, "button");
        doc["command_topic"] = "smartbox/command";
        doc["payload_press"] = b.cmd;
        doc["icon"] = b.icon;
        pubConfig("button", b.id, doc);
    }

    // 2. Sensors
    // State
    {
        JsonDocument doc = createDiscoveryDoc("[SmartBox] 작동 상태", "state", "sensor");
        doc["state_topic"] = "smartbox/event/state";
        doc["value_template"] = "{{ value_json.new_state }}";
        doc["icon"] = "mdi:robot-outline";
        pubConfig("sensor", "state", doc);
    }
    // Battery
    {
        JsonDocument doc = createDiscoveryDoc("[SmartBox] 배터리 전압", "battery_v", "sensor");
        doc["state_topic"] = "smartbox/event/state";
        doc["value_template"] = "{{ value_json.battery_v | round(2) }}";
        doc["unit_of_meas"] = "V";
        doc["device_class"] = "voltage";
        doc["state_class"] = "measurement";
        doc["icon"] = "mdi:battery-charging";
        pubConfig("sensor", "battery_v", doc);
    }
    // Cycle duration
    {
        JsonDocument doc = createDiscoveryDoc("[SmartBox] 최근 개폐 소요 시간", "cycle_duration", "sensor");
        doc["state_topic"] = "smartbox/event/cycle";
        doc["value_template"] = "{{ (value_json.duration_ms / 1000) | round(1) }}";
        doc["unit_of_meas"] = "s";
        doc["icon"] = "mdi:timer-outline";
        pubConfig("sensor", "cycle_duration", doc);
    }
    // Peak current
    {
        JsonDocument doc = createDiscoveryDoc("[SmartBox] 최근 동작 피크 전류", "peak_current", "sensor");
        doc["state_topic"] = "smartbox/event/cycle";
        doc["value_template"] = "{{ value_json.peak_current_ma | round(0) }}";
        doc["unit_of_meas"] = "mA";
        doc["icon"] = "mdi:current-ac";
        pubConfig("sensor", "peak_current", doc);
    }
    // Distance
    {
        JsonDocument doc = createDiscoveryDoc("[SmartBox] 센서 거리", "distance", "sensor");
        doc["state_topic"] = "smartbox/telemetry";
        doc["value_template"] = "{{ value_json.distance_cm }}";
        doc["unit_of_meas"] = "cm";
        doc["icon"] = "mdi:ruler";
        pubConfig("sensor", "distance", doc);
    }
    // WiFi RSSI
    {
        JsonDocument doc = createDiscoveryDoc("[SmartBox] 와이파이 신호 강도", "wifi_rssi", "sensor");
        doc["state_topic"] = "smartbox/telemetry";
        doc["value_template"] = "{{ value_json.wifi_rssi }}";
        doc["unit_of_meas"] = "dBm";
        doc["device_class"] = "signal_strength";
        doc["icon"] = "mdi:wifi";
        pubConfig("sensor", "wifi_rssi", doc);
    }

    // 3. Binary Sensors
    // Connectivity
    {
        JsonDocument doc = createDiscoveryDoc("[SmartBox] 연결 상태", "status", "binary_sensor");
        doc["state_topic"] = "smartbox/status";
        doc["value_template"] = "{% if value_json.status == 'online' %}ON{% else %}OFF{% endif %}";
        doc["device_class"] = "connectivity";
        pubConfig("binary_sensor", "status", doc);
    }
    // Motion
    {
        JsonDocument doc = createDiscoveryDoc("[SmartBox] 모션 감지", "motion", "binary_sensor");
        doc["state_topic"] = "smartbox/event/motion";
        doc["value_template"] = "{% if value_json.event == 'motion_detected' %}ON{% else %}OFF{% endif %}";
        doc["off_delay"] = 3;
        doc["device_class"] = "motion";
        pubConfig("binary_sensor", "motion", doc);
    }
}
