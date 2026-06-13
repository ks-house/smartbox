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
      batteryVoltage(12.0f), motorCurrent(0.0f), currentDistance(999.0f), relaysIsolated(false),
      initialState(STATE_IDLE) {
    for (int i = 0; i < FILTER_SIZE; i++) {
        distBuffer[i] = 999.0f;
    }
}

void SmartBoxController::init() {
    // 1. Set main relay to high-impedance mode first to prevent glitches and reset standby current
    hw.setPinMode(RELAY_MAIN_PIN, INPUT);
    
    // 2. Set direction pins to INPUT_PULLUP to pull them HIGH safely, then convert to OUTPUT
    hw.setPinMode(RELAY_DIR_A_PIN, INPUT_PULLUP);
    hw.setPinMode(RELAY_DIR_B_PIN, INPUT_PULLUP);
    hw.setPinMode(RELAY_DIR_A_PIN, OUTPUT);
    hw.setPinMode(RELAY_DIR_B_PIN, OUTPUT);
    hw.writePin(RELAY_DIR_A_PIN, HIGH);
    hw.writePin(RELAY_DIR_B_PIN, HIGH);
    
    // 3. Setup ultrasonic pins
    hw.setPinMode(TRIG_PIN, OUTPUT);
    hw.setPinMode(ECHO_PIN, INPUT);
    hw.writePin(TRIG_PIN, LOW);
    
    currentState = initialState;
    isCooldown = false;
    relaysIsolated = false;
    
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
            Serial.printf("[BATTERY] CRITICAL LOW VOLTAGE DETECTED! Voltage: %.2f V (Limit: %.2f V) for 3+ seconds.\n", batteryVoltage, config.voltageShutdownLimit);
            Serial.printf("[DIAGNOSTIC] Initializing emergency lid opening. State: %d, Curr: %.1f mA, Dist: %.1f cm\n",
                          currentState, motorCurrent, currentDistance);
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
        case STATE_STARTUP_OPEN:
            setRelayStates(false, false, false);
            if (currentDistance > 0 && currentDistance < config.distThreshold && !isCooldown) {
                Serial.printf("[SENSOR] Startup open: Human approach detected: %.1f cm. Starting 10s wait to close.\n", currentDistance);
                transitionTo(STATE_HOLD);
            }
            break;

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
            
        case STATE_OPENING: {
            // Bypass motor inrush current for the first 500ms (dual actuator startup)
            static int openStallCount = 0;
            if (hw.getMillis() - stateTimer > 500) {
                if (motorCurrent > config.currentStallLimit) {
                    openStallCount++;
                    Serial.printf("[WARN] Stall candidate OPEN (%d/3): %.1f mA (Limit: %.1f mA)\n",
                                  openStallCount, motorCurrent, config.currentStallLimit);
                    if (openStallCount >= 3) {
                        Serial.printf("[EMERGENCY] MOTOR STALL DETECTED DURING OPEN! Current: %.1f mA (Limit: %.1f mA)\n", motorCurrent, config.currentStallLimit);
                        Serial.printf("[DIAGNOSTIC] Time Elapsed: %lu ms, Batt: %.2f V, Dist: %.1f cm\n",
                                      hw.getMillis() - stateTimer, batteryVoltage, currentDistance);
                        openStallCount = 0;
                        forceAllRelaysOff();
                        transitionTo(STATE_EMERGENCY_STOP);
                        break;
                    }
                } else {
                    openStallCount = 0; // Current normal — reset counter
                }
            } else {
                openStallCount = 0; // Reset on re-entry
            }
            if (hw.getMillis() - stateTimer >= config.actuatorTime) {
                Serial.println("[STATE] Opening completed. Entering hold.");
                openStallCount = 0;
                setRelayStates(false, false, false);
                transitionTo(STATE_HOLD);
            }
            break;
        }
            
        case STATE_HOLD:
            setRelayStates(false, false, false);
            // If human is still detected within the threshold, reset the timer to keep the lid open
            if (currentDistance > 0.0f && currentDistance < config.distThreshold) {
                stateTimer = hw.getMillis();
            }
            
            // Wait until the hold time (10 seconds by default) has elapsed without human presence
            if (hw.getMillis() - stateTimer >= config.waitTime) {
                Serial.println("[STATE] Hold timeout reached. Starting closing.");
                transitionTo(STATE_CLOSE_START);
            }
            break;
            
        case STATE_CLOSE_START:
            Serial.println("[STATE] Closing Started. Toggling H-Bridge.");
            setRelayStates(true, false, true); // Main ON, Reverse (A=OFF, B=ON)
            transitionTo(STATE_CLOSING);
            break;
            
        case STATE_CLOSING: {
            // Bypass motor inrush current for the first 500ms (dual actuator startup)
            static int closeStallCount = 0;
            if (hw.getMillis() - stateTimer > 500) {
                if (motorCurrent > config.currentStallLimit) {
                    closeStallCount++;
                    Serial.printf("[WARN] Stall candidate CLOSE (%d/3): %.1f mA (Limit: %.1f mA)\n",
                                  closeStallCount, motorCurrent, config.currentStallLimit);
                    if (closeStallCount >= 3) {
                        Serial.printf("[EMERGENCY] MOTOR STALL DETECTED DURING CLOSE! Current: %.1f mA (Limit: %.1f mA)\n", motorCurrent, config.currentStallLimit);
                        Serial.printf("[DIAGNOSTIC] Time Elapsed: %lu ms, Batt: %.2f V, Dist: %.1f cm\n",
                                      hw.getMillis() - stateTimer, batteryVoltage, currentDistance);
                        closeStallCount = 0;
                        forceAllRelaysOff();
                        transitionTo(STATE_EMERGENCY_STOP);
                        break;
                    }
                } else {
                    closeStallCount = 0; // Current normal — reset counter
                }
            } else {
                closeStallCount = 0; // Reset on re-entry
            }
            // Safety reopen if human is detected during close
            if (currentDistance > 0 && currentDistance < config.distThreshold) {
                Serial.printf("[SAFETY] Human re-approach during closing: %.1f cm! Reopening.\n", currentDistance);
                closeStallCount = 0;
                setRelayStates(false, false, false);
                transitionTo(STATE_OPEN_START);
                break;
            }

            if (hw.getMillis() - stateTimer >= config.actuatorTime) {
                Serial.println("[STATE] Closing completed. Entering standby.");
                closeStallCount = 0;
                setRelayStates(false, false, false);
                isCooldown = true;
                cooldownTimer = hw.getMillis();
                transitionTo(STATE_IDLE);
            }
            break;
        }
            
        case STATE_EMERGENCY_STOP:
            forceAllRelaysOff();
            // Auto-recovery after emergencyRecoveryTime (default 5s)
            if (hw.getMillis() - stateTimer >= config.emergencyRecoveryTime) {
                Serial.printf("[RECOVERY] Emergency lock expired (%lu ms). Checking sensor...\n",
                              config.emergencyRecoveryTime);
                relaysIsolated = false;
                isCooldown = true;
                cooldownTimer = hw.getMillis();
                if (currentDistance > 0.0f && currentDistance < config.distThreshold) {
                    Serial.printf("[RECOVERY] Human detected (%.1f cm) → Auto-opening lid.\n", currentDistance);
                    transitionTo(STATE_OPEN_START);
                } else {
                    Serial.println("[RECOVERY] No human detected → Returning to IDLE.");
                    transitionTo(STATE_IDLE);
                }
            }
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
        relaysIsolated = false;
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
        Serial.println("[EMERGENCY] INTERLOCK BLOCK DETECTED! Both directions active (dirOpen=true, dirClose=true) simultaneously.");
        Serial.printf("[DIAGNOSTIC] State: %d, Batt: %.2f V, Curr: %.1f mA, Dist: %.1f cm\n",
                      currentState, batteryVoltage, motorCurrent, currentDistance);
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
        relaysIsolated = false; // Reset the isolation flag when active control resumes
    } else {
        hw.setPinMode(RELAY_MAIN_PIN, INPUT); // OFF (INPUT high-impedance mode to cut standby draw)
    }
}

void SmartBoxController::forceAllRelaysOff() {
    hw.setPinMode(RELAY_MAIN_PIN, INPUT);
    hw.setPinMode(RELAY_DIR_A_PIN, INPUT);
    hw.setPinMode(RELAY_DIR_B_PIN, INPUT);
    if (!relaysIsolated) {
        relaysIsolated = true;
        Serial.println("[SAFETY] All relays forced OFF and isolated (0mA standby).");
    }
}


