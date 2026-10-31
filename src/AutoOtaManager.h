#ifndef AUTO_OTA_MANAGER_H
#define AUTO_OTA_MANAGER_H

#include "SmartBoxController.h"
#include "secrets.h"
#include <atomic>
#include <mutex>

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

    struct OtaDiagnostics {
        uint32_t attempts;
        const char* phase;
        int manifestHttpCode;
        int updaterError;
        bool requestPending;
    };
    static OtaDiagnostics getDiagnostics();

private:
    enum OtaPhase { OTA_PHASE_IDLE, OTA_PHASE_VERSION, OTA_PHASE_FIRMWARE,
                    OTA_PHASE_REBOOT_PENDING };
    static std::atomic<OtaPhase> otaPhase;
    static std::atomic<uint32_t> otaAttempts;
    static std::atomic<int> otaManifestHttpCode;
    static std::atomic<int> otaUpdaterError;
    static SmartBoxController* controllerPtr;
    static unsigned long lastScheduleCheck;
    static int lastOtaRunDay;
    static unsigned long lastOtaRunTime;  // millis() of last OTA run (0 = never)
    static bool otaForceRequested;
    static bool otaRecoveryRequested;
    static std::mutex requestMutex;
    static bool takeManualRequest(bool normalReady,bool& recovery);
    static bool claimAutomaticRequest();
    static uint32_t recoveryRequestAt,recoveryRequestTtl;
    static bool recoveryRequestFresh();
    static std::atomic<bool> otaInProgress;
    
    static std::atomic<OtaState> otaState;
    static char otaErrorMessage[128];
    
    static void runOtaProcess(bool force, bool sensorFaultRecovery=false);
    static String parseJsonField(const String& json, const String& key);

public:
    static void init(SmartBoxController& controller);
    static void update();
    static bool startOtaUpdate(bool force = false, bool sensorFaultRecovery=false, uint32_t recoveryTtlMs=120000);
    static bool isOtaInProgress() { return otaInProgress; }
    
    static OtaState getOtaState() { return otaState; }
    static String getOtaStateString();
    static String getOtaErrorMessage() { return String(otaErrorMessage); }
    
    static constexpr const char* VERSION_URL = SECRET_VERSION_URL;
    static constexpr const char* FIRMWARE_URL = SECRET_FIRMWARE_URL;
};

#endif // AUTO_OTA_MANAGER_H

