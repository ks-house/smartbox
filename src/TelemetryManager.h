#ifndef TELEMETRY_MANAGER_H
#define TELEMETRY_MANAGER_H

#include "SmartBoxController.h"

class TelemetryManager {
private:
    static SmartBoxController* controllerPtr;
    static unsigned long lastSendTime;

public:
    static void init(SmartBoxController& controller);
    static void update();
};

#endif // TELEMETRY_MANAGER_H
