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
    STATE_BATTERY_LOW_SHUTDOWN
};

struct BoxConfig {
    float distThreshold = 50.0f;
    unsigned long actuatorTime = 3800;
    unsigned long waitTime = 5000;
    unsigned long cooldownTime = 3000;
    float voltageShutdownLimit = 11.3f;
    float currentStallLimit = 800.0f;
};

typedef void (*StateChangeCallback)(State prevState, State newState);

class SmartBoxController {
private:
    HardwareInterface& hw;
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
    
    float batteryVoltage;
    float motorCurrent;
    float currentDistance;
    
    void transitionTo(State newState);
    void updateDistanceBuffer();
    float getFilteredDistance();
    void readSensors();
    void setRelayStates(bool mainOn, bool dirOpen, bool dirClose);
    void forceAllRelaysOff();

public:
    SmartBoxController(HardwareInterface& hardware);
    ~SmartBoxController() {}
    
    void init();
    void update();
    
    State getCurrentState() const { return currentState; }
    void setConfig(const BoxConfig& newConfig) { config = newConfig; }
    BoxConfig getConfig() const { return config; }
    void registerStateCallback(StateChangeCallback cb) { stateCallback = cb; }
    
    void forceOpen();
    void resetEmergency();
    
    float getBatteryVoltage() const { return batteryVoltage; }
    float getMotorCurrent() const { return motorCurrent; }
    float getDistance() const { return currentDistance; }
};

#endif // SMART_BOX_CONTROLLER_H
