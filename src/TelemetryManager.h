#ifndef TELEMETRY_MANAGER_H
#define TELEMETRY_MANAGER_H

#include "SmartBoxController.h"
#include "RemoteLogger.h"
#include <stdint.h>

struct TelemetryData {
    float battery_v;
    float distance_cm;
    float motor_current;
    int state;
    int wifi_rssi;
    unsigned long timestamp_ms;
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

    // Wakeup telemetry
    static bool wakeupTelemetryPending;

    static void dispatchMqttBatch(const TelemetryData* data, int count, const char* batchType);

public:
    static void init(SmartBoxController& controller);
    static void update();
    static void notifySleepEnd();
};

#endif // TELEMETRY_MANAGER_H
