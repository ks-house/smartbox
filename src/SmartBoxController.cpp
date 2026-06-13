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
    // Read ultrasonic distance every 50ms and update buffer
    if (hw.getMillis() - sensorTimer >= 50) {
        sensorTimer = hw.getMillis();
        updateDistanceBuffer();
        currentDistance = getFilteredDistance();
    }
    
    // Read INA219 battery/motor stats every 200ms
    static unsigned long lastReading = 0;
    if (hw.getMillis() - lastReading >= 200) {
        lastReading = hw.getMillis();
        readSensors();
    }
    
    // Battery low voltage 3-second guard filter
    static unsigned long lowVoltGuardStart = 0;
    if (batteryVoltage < config.voltageShutdownLimit && currentState != STATE_BATTERY_LOW_SHUTDOWN) {
        if (lowVoltGuardStart == 0) {
            lowVoltGuardStart = hw.getMillis();
        } else if (hw.getMillis() - lowVoltGuardStart >= 3000) {
            Serial.printf("[BATTERY] Critical battery voltage: %.2fV! Launching Shutdown.\n", batteryVoltage);
            transitionTo(STATE_BATTERY_LOW_SHUTDOWN);
            // Immediately start opening the lid using the remaining power
            setRelayStates(true, true, false);
        }
    } else {
        lowVoltGuardStart = 0;
    }
    
    // Cooldown management

    if (isCooldown && (hw.getMillis() - cooldownTimer >= config.cooldownTime)) {
        isCooldown = false;
        Serial.println("[SYSTEM] Cooldown active state finished.");
    }
    
    switch (currentState) {
        case STATE_IDLE:
            setRelayStates(false, false, false);
            if (currentDistance > 0 && currentDistance < config.distThreshold && !isCooldown) {
                Serial.printf("[SENSOR] Human approach detected: %.1f cm. Starting opening.\n", currentDistance);
                transitionTo(STATE_OPEN_START);
            }
            break;
            
        case STATE_OPEN_START:
            Serial.println("[STATE] Opening Started. Toggling H-Bridge.");
            setRelayStates(true, true, false); // Main ON, Forward (A=ON, B=OFF)
            transitionTo(STATE_OPENING);
            break;
            
        case STATE_OPENING:
            // Bypass motor inrush current for the first 300ms
            if (hw.getMillis() - stateTimer > 300) {
                if (motorCurrent > config.currentStallLimit) {
                    Serial.printf("[EMERGENCY] Stall current detected: %.1f mA. Lock active.\n", motorCurrent);
                    forceAllRelaysOff();
                    transitionTo(STATE_EMERGENCY_STOP);
                    break;
                }
            }
            if (hw.getMillis() - stateTimer >= config.actuatorTime) {
                Serial.println("[STATE] Opening completed. Entering hold.");
                setRelayStates(false, false, false);
                transitionTo(STATE_HOLD);
            }
            break;
            
        case STATE_HOLD:
            setRelayStates(false, false, false);
            if (currentDistance > config.distThreshold) {
                Serial.printf("[SENSOR] Human departed: %.1f cm. Closing early.\n", currentDistance);
                transitionTo(STATE_CLOSE_START);
            } else if (hw.getMillis() - stateTimer >= config.waitTime) {
                Serial.println("[STATE] Hold timeout reached. Starting closing.");
                transitionTo(STATE_CLOSE_START);
            }
            break;
            
        case STATE_CLOSE_START:
            Serial.println("[STATE] Closing Started. Toggling H-Bridge.");
            setRelayStates(true, false, true); // Main ON, Reverse (A=OFF, B=ON)
            transitionTo(STATE_CLOSING);
            break;
            
        case STATE_CLOSING:
            // Bypass motor inrush current for the first 300ms
            if (hw.getMillis() - stateTimer > 300) {
                if (motorCurrent > config.currentStallLimit) {
                    Serial.printf("[EMERGENCY] Stall current detected during close: %.1f mA. Lock active.\n", motorCurrent);
                    forceAllRelaysOff();
                    transitionTo(STATE_EMERGENCY_STOP);
                    break;
                }
            }
            // Safety reopen if human is detected during close
            if (currentDistance > 0 && currentDistance < config.distThreshold) {
                Serial.printf("[SAFETY] Human re-approach during closing: %.1f cm! Reopening.\n", currentDistance);
                setRelayStates(false, false, false);
                transitionTo(STATE_OPEN_START);
                break;
            }

            if (hw.getMillis() - stateTimer >= config.actuatorTime) {
                Serial.println("[STATE] Closing completed. Entering standby.");
                setRelayStates(false, false, false);
                isCooldown = true;
                cooldownTimer = hw.getMillis();
                transitionTo(STATE_IDLE);
            }
            break;
            
        case STATE_EMERGENCY_STOP:
            forceAllRelaysOff();
            break;
            
        case STATE_BATTERY_LOW_SHUTDOWN:
            // After 3.8s, isolate all relays to high-impedance mode
            if (hw.getMillis() - stateTimer >= config.actuatorTime) {
                forceAllRelaysOff();
            }
            break;
    }
}

void SmartBoxController::forceOpen() {
    if (currentState != STATE_BATTERY_LOW_SHUTDOWN && currentState != STATE_EMERGENCY_STOP) {
        transitionTo(STATE_OPEN_START);
    }
}

void SmartBoxController::resetEmergency() {
    if (currentState == STATE_EMERGENCY_STOP) {
        currentState = STATE_IDLE;
        isCooldown = true;
        cooldownTimer = hw.getMillis();
        Serial.println("[SYSTEM] Emergency Lock released. Re-entering IDLE.");
    }
}

void SmartBoxController::transitionTo(State newState) {
    State prev = currentState;
    currentState = newState;
    stateTimer = hw.getMillis();
    
    if (stateCallback != nullptr) {
        stateCallback(prev, newState);
    }
    
    Serial.printf("[STATE] Transition: %d -> %d\n", prev, newState);
}

void SmartBoxController::updateDistanceBuffer() {
    float raw = hw.getDistanceCm();
    distBuffer[filterIdx] = raw;
    filterIdx = (filterIdx + 1) % FILTER_SIZE;
}

float SmartBoxController::getFilteredDistance() {
    float sorted[FILTER_SIZE];
    for (int i = 0; i < FILTER_SIZE; i++) {
        sorted[i] = distBuffer[i];
    }
    // Bubble sort
    for (int i = 0; i < FILTER_SIZE - 1; i++) {
        for (int j = i + 1; j < FILTER_SIZE; j++) {
            if (sorted[i] > sorted[j]) {
                float temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }
    return sorted[FILTER_SIZE / 2];
}

void SmartBoxController::readSensors() {
    batteryVoltage = hw.getBatteryVoltage();
    motorCurrent = hw.getMotorCurrent();
}

void SmartBoxController::setRelayStates(bool mainOn, bool dirOpen, bool dirClose) {
    // Interlock Guard 1: Mutex Lock on direction relays
    if (dirOpen && dirClose) {
        Serial.println("[EMERGENCY] Interlock block! Both directions active. Forcing OFF.");
        forceAllRelaysOff();
        transitionTo(STATE_EMERGENCY_STOP);
        return;
    }

    static bool lastDirOpen = false;
    static bool lastDirClose = false;
    
    bool dirChanged = (dirOpen != lastDirOpen) || (dirClose != lastDirClose);
    
    if (dirChanged) {
        // Interlock Guard 2: Turn off main relay and wait for contact release (100ms)
        hw.setPinMode(RELAY_MAIN_PIN, INPUT);
        hw.delayMs(100);
        
        // Write directions (Active-Low)
        if (dirOpen) {
            hw.writePin(RELAY_DIR_A_PIN, LOW);
            hw.writePin(RELAY_DIR_B_PIN, HIGH);
        } else if (dirClose) {
            hw.writePin(RELAY_DIR_A_PIN, HIGH);
            hw.writePin(RELAY_DIR_B_PIN, LOW);
        } else {
            hw.writePin(RELAY_DIR_A_PIN, HIGH);
            hw.writePin(RELAY_DIR_B_PIN, HIGH);
        }
        
        // Wait for contact absorption (100ms)
        hw.delayMs(100);
        
        lastDirOpen = dirOpen;
        lastDirClose = dirClose;
    }
    
    // Control Main Power
    if (mainOn) {
        hw.setPinMode(RELAY_MAIN_PIN, OUTPUT);
        hw.writePin(RELAY_MAIN_PIN, LOW); // ON (Active-Low)
    } else {
        hw.setPinMode(RELAY_MAIN_PIN, INPUT); // OFF (INPUT high-impedance mode to cut standby draw)
    }
}

void SmartBoxController::forceAllRelaysOff() {
    hw.setPinMode(RELAY_MAIN_PIN, INPUT);
    hw.setPinMode(RELAY_DIR_A_PIN, INPUT);
    hw.setPinMode(RELAY_DIR_B_PIN, INPUT);
    Serial.println("[SAFETY] All relays forced OFF and isolated (0mA standby).");
}


