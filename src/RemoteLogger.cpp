#include "RemoteLogger.h"
#include "MqttManager.h"
#include <stdarg.h>
#include <time.h>

// ── Static member definitions ──────────────────────────────
AsyncWebSocket*                  RemoteLogger::_ws    = nullptr;
LogEntry                         RemoteLogger::_ring[RemoteLogger::RING_SIZE];
int                              RemoteLogger::_head  = 0;
int                              RemoteLogger::_count = 0;
std::mutex                       RemoteLogger::_mutex;
RemoteLogger::LogForwardCallback RemoteLogger::onWarnError = nullptr;

// ── init ───────────────────────────────────────────────────
void RemoteLogger::init(AsyncWebSocket* ws) {
    _ws    = ws;
    _head  = 0;
    _count = 0;
    Serial.println("[RLOG] RemoteLogger initialized. WebSocket endpoint: /api/logs");
}

// ── Internal helpers ───────────────────────────────────────
const char* RemoteLogger::levelStr(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "INFO";
    }
}

// Broadcast a single entry JSON to all connected WS clients and MQTT
void RemoteLogger::broadcastEntry(const LogEntry& entry) {
    if (_ws != nullptr && _ws->count() > 0) {
        // Format: {"ts":12345,"lvl":"INFO","msg":"..."}
        char buf[160];
        snprintf(buf, sizeof(buf),
            "{\"ts\":%lu,\"lvl\":\"%s\",\"msg\":\"%s\"}",
            entry.timestamp_ms,
            levelStr(entry.level),
            entry.message);

        _ws->textAll(buf);
    }
    
    // Also publish to MQTT
    // Include MqttManager.h at the top if not included
    MqttManager::publishLog(levelStr(entry.level), entry.message);
}

// ── log ────────────────────────────────────────────────────
void RemoteLogger::log(LogLevel level, const char* fmt, ...) {
    LogEntry entry;
    entry.timestamp_ms = millis();
    entry.level        = level;

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, sizeof(entry.message), fmt, args);
    va_end(args);

    // Remove trailing newline/CR from message (Serial.printf adds \n)
    int len = strlen(entry.message);
    while (len > 0 && (entry.message[len - 1] == '\n' || entry.message[len - 1] == '\r')) {
        entry.message[--len] = '\0';
    }

    // Thread-safe ring buffer write
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _ring[_head] = entry;
        _head = (_head + 1) % RING_SIZE;
        if (_count < RING_SIZE) _count++;
    }

    // Forward WARN/ERROR to InfluxDB queue via callback
    if ((entry.level == LogLevel::WARN || entry.level == LogLevel::ERROR) && onWarnError != nullptr) {
        onWarnError(entry.level, entry.message);
    }

    // Broadcast to live WebSocket clients (non-blocking, async)
    broadcastEntry(entry);
}

// ── getHistoryJson ─────────────────────────────────────────
String RemoteLogger::getHistoryJson() {
    std::lock_guard<std::mutex> lock(_mutex);

    String json;
    json.reserve(_count * 80 + 4);
    json = "[";

    // Iterate oldest → newest through the ring buffer
    int startIdx = (_count < RING_SIZE) ? 0 : _head;
    for (int i = 0; i < _count; i++) {
        int idx = (startIdx + i) % RING_SIZE;
        if (i > 0) json += ",";
        json += "{\"ts\":";
        json += String(_ring[idx].timestamp_ms);
        json += ",\"lvl\":\"";
        json += levelStr(_ring[idx].level);
        json += "\",\"msg\":\"";
        // Escape double-quotes in message
        for (int c = 0; _ring[idx].message[c] != '\0'; c++) {
            if (_ring[idx].message[c] == '"')  json += "\\\"";
            else if (_ring[idx].message[c] == '\\') json += "\\\\";
            else json += _ring[idx].message[c];
        }
        json += "\"}";
    }
    json += "]";
    return json;
}

// ── WebSocket event handler ────────────────────────────────
void RemoteLogger::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                              AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[RLOG] WebSocket client #%u connected from %s\n",
                      client->id(), client->remoteIP().toString().c_str());
        // Send full log history to the newly connected client
        String history = getHistoryJson();
        client->text("{\"type\":\"history\",\"data\":" + history + "}");
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[RLOG] WebSocket client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_ERROR) {
        Serial.printf("[RLOG] WebSocket client #%u error\n", client->id());
    }
}
