#include "RemoteLogger.h"
#include <stdarg.h>
#include <time.h>

LogEntry                         RemoteLogger::_ring[RemoteLogger::RING_SIZE];
int                              RemoteLogger::_head  = 0;
int                              RemoteLogger::_count = 0;
std::mutex                       RemoteLogger::_mutex;
RemoteLogger::LogForwardCallback RemoteLogger::onWarnError = nullptr;

void RemoteLogger::init() {
    _head  = 0;
    _count = 0;
    Serial.println("[RLOG] RemoteLogger initialized.");
}

const char* RemoteLogger::levelStr(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "INFO";
    }
}

void RemoteLogger::log(LogLevel level, const char* fmt, ...) {
    LogEntry entry;
    entry.timestamp_ms = millis();
    entry.level        = level;

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, sizeof(entry.message), fmt, args);
    va_end(args);

    int len = strlen(entry.message);
    while (len > 0 && (entry.message[len - 1] == '\n' || entry.message[len - 1] == '\r')) {
        entry.message[--len] = '\0';
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _ring[_head] = entry;
        _head = (_head + 1) % RING_SIZE;
        if (_count < RING_SIZE) _count++;
    }

    if ((entry.level == LogLevel::WARN || entry.level == LogLevel::ERROR) && onWarnError != nullptr) {
        onWarnError(entry.level, entry.message);
    }
}

String RemoteLogger::getHistoryJson() {
    std::lock_guard<std::mutex> lock(_mutex);

    String json;
    json.reserve(_count * 80 + 4);
    json = "[";

    int startIdx = (_count < RING_SIZE) ? 0 : _head;
    for (int i = 0; i < _count; i++) {
        int idx = (startIdx + i) % RING_SIZE;
        if (i > 0) json += ",";
        json += "{\"ts\":";
        json += String(_ring[idx].timestamp_ms);
        json += ",\"lvl\":\"";
        json += levelStr(_ring[idx].level);
        json += "\",\"msg\":\"";
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
