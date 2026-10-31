#include "BatteryPolicy.h"
#include <cmath>
#include <algorithm>

void BatteryPolicy::restoreProtection() {
  latched_=true; state_=BatteryState::PROTECTED;
  recoveryTiming_=false; lowTiming_=false; healthyTiming_=false; stateTiming_=false;
}
void BatteryPolicy::sample(uint32_t now,float voltage,bool valid,float cutoff) {
  const bool gap=sampled_ && uint32_t(now-sampleAt_)>kMaxSampleGapMs;
  if(gap) {lowTiming_=false; recoveryTiming_=false; healthyTiming_=false; stateTiming_=false;}
  sampled_=true; sampleAt_=now;
  cutoff_=std::isfinite(cutoff) && cutoff>0 && cutoff<25 ? cutoff : 11.3f;
  recovery_=std::max(12.4f,cutoff_+0.8f);
  valid_=valid && std::isfinite(voltage) && voltage>0 && voltage<=16.0f;
  voltage_=voltage;
  if(!valid_) {
    latched_=true; state_=BatteryState::SENSOR_FAULT;
    lowTiming_=false; recoveryTiming_=false; healthyTiming_=false; stateTiming_=false;
    return;
  }
  if(minimum_==0 || voltage<minimum_) minimum_=voltage;
  if(voltage>=recovery_) {
    if(!healthyTiming_) {healthyTiming_=true; healthyAt_=now;}
  } else healthyTiming_=false;
  if(latched_) {
    if(voltage>=recovery_) {
      if(!recoveryTiming_) {recoveryTiming_=true; recoveryAt_=now;}
      state_=BatteryState::RECOVERING;
      if(uint32_t(now-recoveryAt_)>=kRecoveryMs) {
        latched_=false; recoveryTiming_=false; stateTiming_=false; ++recoveries_;
        state_=BatteryState::NORMAL;
      }
    } else {recoveryTiming_=false; state_=BatteryState::PROTECTED;}
    return;
  }
  if(voltage<cutoff_) {
    if(!lowTiming_) {lowTiming_=true; lowAt_=now;}
    if(uint32_t(now-lowAt_)>=3000) {
      latched_=true; state_=BatteryState::PROTECTED;
      recoveryTiming_=false; stateTiming_=false; return;
    }
  } else lowTiming_=false;
  BatteryState desired=voltage<cutoff_+0.4f ? BatteryState::SAVING :
                       voltage<cutoff_+0.7f ? BatteryState::WARNING : BatteryState::NORMAL;
  if(state_==BatteryState::SAVING && voltage<cutoff_+0.5f) desired=BatteryState::SAVING;
  else if(state_==BatteryState::WARNING && desired==BatteryState::NORMAL && voltage<cutoff_+0.8f) desired=BatteryState::WARNING;
  if(state_==BatteryState::UNKNOWN) {state_=desired;stateTiming_=false;}
  else if(desired==state_) stateTiming_=false;
  else if(!stateTiming_ || candidateState_!=desired) {
    candidateState_=desired;stateAt_=now;stateTiming_=true;
  } else if(uint32_t(now-stateAt_)>=15000) {state_=desired;stateTiming_=false;}
}
BatterySnapshot BatteryPolicy::snapshot(uint32_t now) const {
  const uint32_t age=sampled_ ? uint32_t(now-sampleAt_) : UINT32_MAX;
  const bool fresh=valid_ && age<=kMaxSampleGapMs;
  BatteryState state=state_;
  if(sampled_ && !fresh) state=BatteryState::SENSOR_FAULT;
  const bool blocked=latched_ || !fresh;
  const uint32_t interval=blocked ? 300000 : state==BatteryState::SAVING ? 120000 :
                          state==BatteryState::WARNING ? 60000 : 30000;
  const uint32_t elapsed=recoveryTiming_ ? uint32_t(now-recoveryAt_) : 0;
  return {state,fresh,blocked,fresh && !latched_ && voltage_>=cutoff_,
    fresh && !latched_ && healthyTiming_ && uint32_t(now-healthyAt_)>=30000,
    voltage_,minimum_,recovery_,latched_ ? (elapsed>=kRecoveryMs ? 0 : kRecoveryMs-elapsed) : 0,
    recoveries_,age,interval};
}
const char* BatteryPolicy::name(BatteryState state) {
  switch(state) {
    case BatteryState::NORMAL:return "NORMAL";
    case BatteryState::WARNING:return "WARNING";
    case BatteryState::SAVING:return "SAVING";
    case BatteryState::PROTECTED:return "PROTECTED";
    case BatteryState::RECOVERING:return "RECOVERING";
    case BatteryState::SENSOR_FAULT:return "SENSOR_FAULT";
    default:return "UNKNOWN";
  }
}
