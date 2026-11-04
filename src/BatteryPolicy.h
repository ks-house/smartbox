#ifndef SMARTBOX_BATTERY_POLICY_H
#define SMARTBOX_BATTERY_POLICY_H
#include <stdint.h>

enum class BatteryState : uint8_t {
  NORMAL=0, WARNING=1, SAVING=2, PROTECTED=3, RECOVERING=4,
  SENSOR_FAULT=5, UNKNOWN=6
};
struct BatterySnapshot {
  BatteryState state;
  bool valid;
  bool protectedMode;
  bool actuationAllowed;
  bool otaReady;
  float voltage;
  float minimumVoltage;
  float recoveryVoltage;
  uint32_t recoveryRemainingMs;
  uint32_t recoveryCount;
  uint32_t sampleAgeMs;
  uint32_t telemetryIntervalMs;
  bool forceOtaReady;
  float forceOtaVoltage;
};
class BatteryPolicy {
 public:
  static constexpr float kDefaultCutoffVoltage=11.0f;
  static constexpr uint32_t kRecoveryMs=300000;
  static constexpr uint32_t kMaxSampleGapMs=2000;
  void sample(uint32_t now,float voltage,bool valid,float cutoff);
  void restoreProtection();
  BatterySnapshot snapshot(uint32_t now) const;
  static const char* name(BatteryState state);
 private:
  BatteryState state_=BatteryState::UNKNOWN, candidateState_=BatteryState::UNKNOWN;
  bool stateTiming_=false;
  uint32_t stateAt_=0;
  bool valid_=false, latched_=false, sampled_=false;
  bool lowTiming_=false, recoveryTiming_=false, healthyTiming_=false;
  float voltage_=0, minimum_=0, cutoff_=kDefaultCutoffVoltage, recovery_=12.0f;
  bool forceHealthyTiming_=false;
  uint32_t forceHealthyAt_=0;
  float forceFloor_=11.7f;
  uint32_t sampleAt_=0, lowAt_=0, recoveryAt_=0, healthyAt_=0, recoveries_=0;
};
#endif
