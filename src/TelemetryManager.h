#ifndef TELEMETRY_MANAGER_H
#define TELEMETRY_MANAGER_H

#include "SmartBoxController.h"

class TelemetryManager {
private:
    static SmartBoxController* controllerPtr;
    static unsigned long lastSendTime;
    static volatile bool isSending;

    struct TelemetryData {
        float battery_v;
        float distance_cm;
        int state;
        int wifi_rssi;
    };

    static void telemetryTaskFunction(void* pvParameters);

public:
    static void init(SmartBoxController& controller);
    static void update();
};

#endif // TELEMETRY_MANAGER_H
