#ifndef REMOTE_LOGGER_H
#define REMOTE_LOGGER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <mutex>

// ============================================================
//  Log severity levels
// ============================================================
enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

// ============================================================
//  Ring-buffer entry (fixed 120-char message for SRAM safety)
// ============================================================
struct LogEntry {
    unsigned long timestamp_ms;
    LogLevel      level;
    char          message[120];
};

// ============================================================
//  RLOG macro — writes to Serial AND remote logger
//  Usage: RLOG(LogLevel::INFO, "[TAG] format %d", value);
// ============================================================
#define RLOG(lvl, fmt, ...) \
    do { \
        Serial.printf(fmt, ##__VA_ARGS__); \
        RemoteLogger::log(lvl, fmt, ##__VA_ARGS__); \
    } while(0)

// Convenience shorthand macros
#define RLOG_D(fmt, ...) RLOG(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define RLOG_I(fmt, ...) RLOG(LogLevel::INFO,  fmt, ##__VA_ARGS__)
#define RLOG_W(fmt, ...) RLOG(LogLevel::WARN,  fmt, ##__VA_ARGS__)
#define RLOG_E(fmt, ...) RLOG(LogLevel::ERROR, fmt, ##__VA_ARGS__)

// ============================================================
//  RemoteLogger — static singleton
// ============================================================
class RemoteLogger {
public:
    static constexpr int RING_SIZE = 100; // Number of lines kept in SRAM

    // Optional callback: called on WARN/ERROR entries for InfluxDB forwarding
    // Signature: void callback(LogLevel level, const char* message)
    using LogForwardCallback = void(*)(LogLevel, const char*);
    static LogForwardCallback onWarnError;

    static void init(AsyncWebSocket* ws);
    static void log(LogLevel level, const char* fmt, ...);

    // Retrieve current buffer as a JSON array string
    static String getHistoryJson();

    // Called by WebSocket event handler (connected clients)
    static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len);

private:
    static AsyncWebSocket*  _ws;
    static LogEntry         _ring[RING_SIZE];
    static int              _head;    // next write index
    static int              _count;   // total entries stored (capped at RING_SIZE)
    static std::mutex       _mutex;

    static const char* levelStr(LogLevel lvl);
    static void broadcastEntry(const LogEntry& entry);
};

#endif // REMOTE_LOGGER_H
