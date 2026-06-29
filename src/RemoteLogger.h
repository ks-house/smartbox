#ifndef REMOTE_LOGGER_H
#define REMOTE_LOGGER_H

#include <Arduino.h>
#include <mutex>

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

struct LogEntry {
    unsigned long timestamp_ms;
    LogLevel      level;
    char          message[120];
};

#define RLOG(lvl, fmt, ...) \
    do { \
        Serial.printf(fmt, ##__VA_ARGS__); \
        RemoteLogger::log(lvl, fmt, ##__VA_ARGS__); \
    } while(0)

#define RLOG_D(fmt, ...) RLOG(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define RLOG_I(fmt, ...) RLOG(LogLevel::INFO,  fmt, ##__VA_ARGS__)
#define RLOG_W(fmt, ...) RLOG(LogLevel::WARN,  fmt, ##__VA_ARGS__)
#define RLOG_E(fmt, ...) RLOG(LogLevel::ERROR, fmt, ##__VA_ARGS__)

class RemoteLogger {
public:
    static constexpr int RING_SIZE = 100;

    using LogForwardCallback = void(*)(LogLevel, const char*);
    static LogForwardCallback onWarnError;

    static void init();
    static void log(LogLevel level, const char* fmt, ...);
    static String getHistoryJson();

private:
    static LogEntry   _ring[RING_SIZE];
    static int        _head;
    static int        _count;
    static std::mutex _mutex;

    static const char* levelStr(LogLevel lvl);
};

#endif // REMOTE_LOGGER_H
