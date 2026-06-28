#ifndef TELEMETRY_MANAGER_H
#define TELEMETRY_MANAGER_H

#include "SmartBoxController.h"
#include "RemoteLogger.h"

#include <mutex>

struct TelemetryData {
    float battery_v;
    float distance_cm;
    float motor_current;
    int state;
    int wifi_rssi;
    unsigned long timestamp_ms;
};

#include <stdint.h>

struct BatchPayload {
    TelemetryData* data;
    int count;
    char type[16];
    unsigned long uploadStartMillis;
    uint64_t currentEpochMs;
};

// ── Log entry for InfluxDB forwarding ─────────────────────
struct RemoteLogEntry {
    unsigned long timestamp_ms;
    LogLevel      level;
    char          message[120];
};

class TelemetryManager {
private:
    static SmartBoxController* controllerPtr;
    static unsigned long lastSendTime;

    // Buffer for batch telemetry during motor activity
    static constexpr int MAX_BATCH_SIZE = 30;
    static TelemetryData eventBuffer[MAX_BATCH_SIZE];
    static int bufferCount;
    static bool wasMotorRunning;
    static unsigned long lastSampleTime;

    // Buffer for periodic heartbeat telemetry
    static TelemetryData heartbeatBuffer[60];
    static int heartbeatCount;
    static unsigned long lastHeartbeatSampleTime;

    // Diagnostic Store & Forward state
    static int pendingFailedTxCount;
    static String lastNetworkError;
    static std::mutex diagMutex;

    // ── InfluxDB log forwarding queue (WARN/ERROR only) ───
    static constexpr int MAX_LOG_QUEUE = 20;
    static RemoteLogEntry logQueue[MAX_LOG_QUEUE];
    static int logQueueCount;
    static std::mutex logQueueMutex;
    
    // Wakeup telemetry
    static bool wakeupTelemetryPending;

    static void telemetryTaskFunction(void* pvParameters);

public:
    static void init(SmartBoxController& controller);
    static void update();
    static void notifySleepEnd();

    // Push a log entry to the InfluxDB forwarding queue (thread-safe)
    static void pushLog(LogLevel level, const char* message);
};

#endif // TELEMETRY_MANAGER_H
