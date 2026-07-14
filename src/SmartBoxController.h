#ifndef SMART_BOX_CONTROLLER_H
#define SMART_BOX_CONTROLLER_H

#include "HardwareInterface.h"
#include <mutex>

#ifndef NATIVE_BUILD
#include <Arduino.h>
#else
#include <stdio.h>
class MockSerial {
public:
    void print(const char* s) { printf("%s", s); }
    void println(const char* s) { printf("%s\n", s); }
    template<typename... Args>
    void printf(const char* format, Args... args) {
        ::printf(format, args...);
    }
};
extern MockSerial Serial;
#endif

enum State {
  STATE_IDLE,
  STATE_OPEN_START,
  STATE_OPENING,
  STATE_HOLD,
  STATE_CLOSE_START,
  STATE_CLOSING,
  STATE_EMERGENCY_STOP,
  STATE_BATTERY_LOW_SHUTDOWN,
  STATE_STARTUP_OPEN,
  STATE_OTA_UPDATING,
  STATE_MAINTENANCE
};

// Tuning Constants
static constexpr float OPEN_THRESHOLD_CM = 50.0f;
static constexpr unsigned long DEBOUNCE_DELAY_MS = 300;
static constexpr unsigned long MAINTENANCE_TIMEOUT_MS = 300000; // 5 minutes

struct BoxConfig {
  float distThreshold = OPEN_THRESHOLD_CM;
  unsigned long debounceDelay = DEBOUNCE_DELAY_MS;
  unsigned long actuatorTime = 3800;
  unsigned long waitTime = 10000;
  unsigned long cooldownTime = 3000;
  float voltageShutdownLimit = 11.3f;
  float currentStallLimit =
      3000.0f; // Dual actuator: 100N/60mm/s x2 @ 12V, normal run ~2000mA
  unsigned long emergencyRecoveryTime =
      5000; // Auto-recovery from EMERGENCY_STOP (ms)
  int otaHour = 0; // Changed to 0 (midnight) for safe catch-up check
  int telemetryIntervalMin = 10; // Default: 10 minutes
  unsigned long maxHoldTime = 180000; // 3 minutes
};

typedef void (*StateChangeCallback)(State prevState, State newState);

class SmartBoxController {
private:
  HardwareInterface &hw;
  State currentState;
  unsigned long stateTimer;
  unsigned long sensorTimer;
  unsigned long cooldownTimer;
  bool isCooldown;

  // Median filter buffer and debounce timer
  static const int FILTER_SIZE = 5;
  float distanceHistory[5];
  int distanceHistoryIdx;
  unsigned long lastDetectStartTime;

  BoxConfig config;
  StateChangeCallback stateCallback;
  State initialState;

  float batteryVoltage;
  float motorCurrent;
  float currentDistance;
  bool relaysIsolated;
  bool maintenanceRequested;
  unsigned long holdStartTime;
  bool sensorDeadlockFlag;
  unsigned long deadlockClearStartTime;
  // BUG-02 fix: stall counters as member vars to prevent cross-entry accumulation
  int openStallCount;
  int closeStallCount;
  mutable std::recursive_mutex dataMutex;

  void updateDistanceBuffer();
  float getFilteredDistance();
  void readSensors();
  void setRelayStates(bool mainOn, bool dirOpen, bool dirClose);

public:
  void transitionTo(State newState);
  void forceAllRelaysOff();
  bool isOtaMode() const {
      std::lock_guard<std::recursive_mutex> lock(dataMutex);
      return currentState == STATE_OTA_UPDATING;
  }
  bool getSensorDeadlockFlag() const {
      std::lock_guard<std::recursive_mutex> lock(dataMutex);
      return sensorDeadlockFlag;
  }
  SmartBoxController(HardwareInterface &hardware);
  ~SmartBoxController() {}

  void init();
  void update();

  State getCurrentState() const {
      std::lock_guard<std::recursive_mutex> lock(dataMutex);
      return currentState;
  }
  void setConfig(const BoxConfig &newConfig) { 
      std::lock_guard<std::recursive_mutex> lock(dataMutex);
      config = newConfig; 
  }
  BoxConfig getConfig() const { 
      std::lock_guard<std::recursive_mutex> lock(dataMutex);
      return config; 
  }
  void registerStateCallback(StateChangeCallback cb) { stateCallback = cb; }
  void setInitialState(State state) { initialState = state; }

  void forceOpen();
  void forceClose();
  void resetEmergency();
  void startMaintenanceMode();
  void stopMaintenanceMode();
  unsigned long getMaintenanceRemainingSeconds() const;

#if __has_include("git_version.h")
#include "git_version.h"
#endif

#ifdef FIRMWARE_VERSION_OVERRIDE
  static constexpr const char *FIRMWARE_VERSION = FIRMWARE_VERSION_OVERRIDE;
#else
  static constexpr const char *FIRMWARE_VERSION = "0.1.0";
#endif
  const char *getFirmwareVersion() const { return FIRMWARE_VERSION; }

  float getBatteryVoltage() const {
      std::lock_guard<std::recursive_mutex> lock(dataMutex);
      return batteryVoltage;
  }
  float getMotorCurrent() const {
      std::lock_guard<std::recursive_mutex> lock(dataMutex);
      return motorCurrent;
  }
  float getDistance() const {
      std::lock_guard<std::recursive_mutex> lock(dataMutex);
      return currentDistance;
  }
  bool isMotorRunning() const;
  bool canSendTelemetry() const;
};

#endif // SMART_BOX_CONTROLLER_H
