#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "SmartBoxController.h"

class PowerManager {
private:
    static SmartBoxController* controllerPtr;
    static unsigned long lastCheckTime;

public:
    static void init(SmartBoxController& controller);
    static void update();
};

#endif // POWER_MANAGER_H
