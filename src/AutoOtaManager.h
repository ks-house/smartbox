#ifndef AUTO_OTA_MANAGER_H
#define AUTO_OTA_MANAGER_H

#include "SmartBoxController.h"
#include "secrets.h"

class AutoOtaManager {
public:
    enum OtaState {
        OTA_STATE_IDLE,
        OTA_STATE_CHECKING,
        OTA_STATE_UP_TO_DATE,
        OTA_STATE_UPDATING,
        OTA_STATE_FAILED,
        OTA_STATE_SUCCESS
    };

private:
    static SmartBoxController* controllerPtr;
    static unsigned long lastScheduleCheck;
    static unsigned long lastOtaRunTime;  // millis() of last OTA run (0 = never)
    static volatile bool otaForceRequested;
    static volatile bool otaInProgress;
    
    static volatile OtaState otaState;
    static char otaErrorMessage[128];
    
    static void runOtaProcess(bool force);
    static String parseJsonField(const String& json, const String& key);

public:
    static void init(SmartBoxController& controller);
    static void update();
    static bool startOtaUpdate(bool force = false);
    static bool isOtaInProgress() { return otaInProgress; }
    
    static OtaState getOtaState() { return otaState; }
    static String getOtaStateString();
    static String getOtaErrorMessage() { return String(otaErrorMessage); }
    
    static constexpr const char* VERSION_URL = SECRET_VERSION_URL;
    static constexpr const char* FIRMWARE_URL = SECRET_FIRMWARE_URL;
};

#endif // AUTO_OTA_MANAGER_H

