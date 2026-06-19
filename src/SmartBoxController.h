#ifndef SMART_BOX_CONTROLLER_H
#define SMART_BOX_CONTROLLER_H

#include "HardwareInterface.h"

#ifndef NATIVE_BUILD
#include <Arduino.h>
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
  STATE_OTA_UPDATING
};

struct BoxConfig {
  float distThreshold = 50.0f;
  unsigned long actuatorTime = 3800;
  unsigned long waitTime = 10000;
  unsigned long cooldownTime = 3000;
  float voltageShutdownLimit = 11.3f;
  float currentStallLimit =
      3000.0f; // Dual actuator: 100N/60mm/s x2 @ 12V, normal run ~2000mA
  unsigned long emergencyRecoveryTime =
      5000; // Auto-recovery from EMERGENCY_STOP (ms)
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

  // Median filter buffer
  static const int FILTER_SIZE = 5;
  float distBuffer[FILTER_SIZE];
  int filterIdx;

  BoxConfig config;
  StateChangeCallback stateCallback;
  State initialState;

  float batteryVoltage;
  float motorCurrent;
  float currentDistance;
  bool relaysIsolated;

  void updateDistanceBuffer();
  float getFilteredDistance();
  void readSensors();
  void setRelayStates(bool mainOn, bool dirOpen, bool dirClose);

public:
  void transitionTo(State newState);
  void forceAllRelaysOff();
  bool isOtaMode() const { return currentState == STATE_OTA_UPDATING; }
  SmartBoxController(HardwareInterface &hardware);
  ~SmartBoxController() {}

  void init();
  void update();

  State getCurrentState() const { return currentState; }
  void setConfig(const BoxConfig &newConfig) { config = newConfig; }
  BoxConfig getConfig() const { return config; }
  void registerStateCallback(StateChangeCallback cb) { stateCallback = cb; }
  void setInitialState(State state) { initialState = state; }

  void forceOpen();
  void resetEmergency();

  static constexpr const char *FIRMWARE_VERSION = "1.0.0";
  const char *getFirmwareVersion() const { return FIRMWARE_VERSION; }

  float getBatteryVoltage() const { return batteryVoltage; }
  float getMotorCurrent() const { return motorCurrent; }
  float getDistance() const { return currentDistance; }
};

#endif // SMART_BOX_CONTROLLER_H
