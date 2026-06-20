#ifndef TELEMETRY_MANAGER_H
#define TELEMETRY_MANAGER_H

#include "SmartBoxController.h"

#include <mutex>

struct TelemetryData {
    float battery_v;
    float distance_cm;
    float motor_current;
    int state;
    unsigned long timestamp_ms;
};

struct BatchPayload {
    TelemetryData* data;
    int count;
    char type[16];
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

    static void telemetryTaskFunction(void* pvParameters);

public:
    static void init(SmartBoxController& controller);
    static void update();
};

#endif // TELEMETRY_MANAGER_H
