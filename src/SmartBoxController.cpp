#include "SmartBoxController.h"

// Fixed pin assignments as per AGENTS.md
static const int RELAY_MAIN_PIN = 6;
static const int RELAY_DIR_A_PIN = 7;
static const int RELAY_DIR_B_PIN = 8;
static const int TRIG_PIN = 4;
static const int ECHO_PIN = 5;

SmartBoxController::SmartBoxController(HardwareInterface& hardware) 
    : hw(hardware), currentState(STATE_IDLE), stateTimer(0), sensorTimer(0), 
      cooldownTimer(0), isCooldown(false), filterIdx(0), stateCallback(nullptr),
      batteryVoltage(12.0f), motorCurrent(0.0f), currentDistance(999.0f) {
    for (int i = 0; i < FILTER_SIZE; i++) {
        distBuffer[i] = 999.0f;
    }
}

void SmartBoxController::init() {
    // 1. Set main relay to high-impedance mode first to prevent glitches and reset standby current
    hw.setPinMode(RELAY_MAIN_PIN, INPUT);
    
    // 2. Pre-write HIGH (OFF for Active-Low) to direction pins, then set to OUTPUT to avoid glitch triggers
    hw.writePin(RELAY_DIR_A_PIN, HIGH);
    hw.writePin(RELAY_DIR_B_PIN, HIGH);
    hw.setPinMode(RELAY_DIR_A_PIN, OUTPUT);
    hw.setPinMode(RELAY_DIR_B_PIN, OUTPUT);
    
    // 3. Setup ultrasonic pins
    hw.setPinMode(TRIG_PIN, OUTPUT);
    hw.setPinMode(ECHO_PIN, INPUT);
    hw.writePin(TRIG_PIN, LOW);
    
    currentState = STATE_IDLE;
    isCooldown = false;
    
    // Fill median filter buffer
    for (int i = 0; i < FILTER_SIZE; i++) {
        distBuffer[i] = 999.0f;
    }
}


void SmartBoxController::update() {
    // Stub
}

void SmartBoxController::forceOpen() {}
void SmartBoxController::resetEmergency() {}
void SmartBoxController::transitionTo(State newState) {}
void SmartBoxController::updateDistanceBuffer() {}
float SmartBoxController::getFilteredDistance() { return 999.0f; }
void SmartBoxController::readSensors() {}
void SmartBoxController::setRelayStates(bool mainOn, bool dirOpen, bool dirClose) {}
void SmartBoxController::forceAllRelaysOff() {}
