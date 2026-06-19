#ifndef AUTO_OTA_MANAGER_H
#define AUTO_OTA_MANAGER_H

#include "SmartBoxController.h"

class AutoOtaManager {
private:
    static SmartBoxController* controllerPtr;
    static unsigned long lastScheduleCheck;
    static int lastOtaCheckDay;
    
    static void otaTaskFunction(void *pvParameters);
    static String parseJsonField(const String& json, const String& key);

public:
    static void init(SmartBoxController& controller);
    static void update();
    static bool startOtaUpdate(bool force = false);
    
    static constexpr const char* VERSION_URL = "***REMOVED***";
    static constexpr const char* FIRMWARE_URL = "***REMOVED***";
};

#endif // AUTO_OTA_MANAGER_H
