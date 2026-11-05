#include "SmartBoxController.h"
#include <algorithm>

// Fixed pin assignments as per AGENTS.md
static const int RELAY_MAIN_PIN = 6;
static const int RELAY_DIR_A_PIN = 7;
static const int RELAY_DIR_B_PIN = 8;
static const int TRIG_PIN = 4;
static const int ECHO_PIN = 5;

SmartBoxController::SmartBoxController(HardwareInterface& hardware) 
    : hw(hardware), currentState(STATE_IDLE), stateTimer(0), sensorTimer(0), 
      cooldownTimer(0), isCooldown(false), distanceHistoryIdx(0), lastDetectStartTime(0), stateCallback(nullptr),
      batteryVoltage(12.0f), motorCurrent(0.0f), currentDistance(999.0f), relaysIsolated(false),
      initialState(STATE_IDLE), maintenanceRequested(false),
      holdStartTime(0), sensorDeadlockFlag(false), deadlockClearStartTime(0), postCloseBlindStartTime(0),
      openStallCount(0), closeStallCount(0), openingRelayStarted(false),
      lastDirOpen(false), lastDirClose(false), isCalibrationPhase(false) {
    for (int i = 0; i < 5; i++) {
        distanceHistory[i] = 999.0f;
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
    distanceHistoryIdx = 0;
    lastDetectStartTime = 0;
    maintenanceRequested = false;
    holdStartTime = 0;
    sensorDeadlockFlag = false;
    deadlockClearStartTime = 0;
    postCloseBlindStartTime = 0;
    openStallCount = 0;
    closeStallCount = 0;
    openingRelayStarted = false;
    lastDirOpen = false;
    lastDirClose = false;
    isCalibrationPhase = false;
    
    lastPowerReading=hw.getMillis();
    lowBatteryOpening=false;
    if(initialState==STATE_BATTERY_LOW_SHUTDOWN) batteryPolicy.restoreProtection();
    readSensors();

    // Fill median filter buffer
    for (int i = 0; i < 5; i++) {
        distanceHistory[i] = 999.0f;
    }
}


void SmartBoxController::update() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);

    if(servicePower()) return;
    if(currentState==STATE_OTA_UPDATING) return;
    const auto power=powerSnapshot();
    const unsigned long distancePeriod=power.state==BatteryState::SAVING ? 100 : 50;
    if(hw.getMillis()-sensorTimer>=distancePeriod) {
        sensorTimer=hw.getMillis(); updateDistanceBuffer();
        currentDistance=getFilteredDistance();
    }

    // Cooldown management

    if (isCooldown && (hw.getMillis() - cooldownTimer >= config.cooldownTime)) {
        isCooldown = false;
        Serial.println("[SYSTEM] Cooldown active state finished.");
    }
    
    switch (currentState) {
        case STATE_STARTUP_OPEN:
            // [FIX-1] 부팅 시 뚜껑 상태 불확실 → 500ms 안정화 후 항상 닫힘 위치로 홈(Homing)
            // 이전: 사람 감지 시 OPENING 없이 HOLD로 직행(IDLE→HOLD 처럼 보이는 문제)
            // 변경: STARTUP_OPEN → OPEN_START로 이동하여 완전 캘리브레이션 1회 수행
            setRelayStates(false, false, false);
            if (hw.getMillis() - stateTimer >= 500) {
                Serial.println("[STATE] STARTUP_OPEN: Starting calibration phase (OPEN -> HOLD -> CLOSE).");
                isCalibrationPhase = true;
                transitionTo(STATE_OPEN_START);
            }
            break;

        case STATE_IDLE: {
            setRelayStates(false, false, false);
            
            if (isCalibrationPhase) {
                Serial.println("[STATE] Calibration phase complete. Resuming normal operation.");
                isCalibrationPhase = false;
            }

            // Self-healing: Path must be clean CONTINUOUSLY for 10 seconds to clear deadlock
            if (currentDistance > config.distThreshold) {
                if (sensorDeadlockFlag) {
                    if (deadlockClearStartTime == 0) {
                        deadlockClearStartTime = hw.getMillis();
                    } else if (hw.getMillis() - deadlockClearStartTime >= 10000) {
                        Serial.printf("[SENSOR] Deadlock cleared. Path consistently clean for 10s: %.1f cm. Self-healing active.\n", currentDistance);
                        sensorDeadlockFlag = false;
                        deadlockClearStartTime = 0;
                    }
                }
            } else {
                // 거리가 다시 임계치 아래로 떨어지면(노이즈 발생) 데드락 해제 타이머 초기화
                deadlockClearStartTime = 0;
            }
            
            // 3초(3000ms) 블라인드 타임 (문 닫힘 직후 기구물 안정화 대기)
            bool isBlindTime = (postCloseBlindStartTime != 0) && (hw.getMillis() - postCloseBlindStartTime < 3000);
            
            if (isBlindTime) {
                // 블라인드 타임 중에는 센서 감지 타이머를 계속 초기화하여 재개방 차단
                lastDetectStartTime = 0;
            } else {
                if (currentDistance > 2.0f && currentDistance < config.distThreshold && !isCooldown) { 
                    // 2.0f 이하의 초근접 값은 센서 결로/오류(0.0 등)이므로 무시
                    if (!sensorDeadlockFlag) {
                        if (lastDetectStartTime == 0) {
                            lastDetectStartTime = hw.getMillis();
                        } else if (hw.getMillis() - lastDetectStartTime >= config.debounceDelay) {
                            Serial.printf("[SENSOR] Human approach detected: %.1f cm for %lu ms. Starting opening.\n",
                                          currentDistance, hw.getMillis() - lastDetectStartTime);
                            lastDetectStartTime = 0;
                            transitionTo(STATE_OPEN_START);
                        }
                    } else {
                        lastDetectStartTime = 0;
                    }
                } else {
                    lastDetectStartTime = 0;
                }
            }
            break;
        }

            
        case STATE_OPEN_START:
            // OPEN_START: 릴레이 설정 후 transitionTo(OPENING)
            // setRelayStates() 내 delayMs(100)×2가 실제 하드웨어/모직에서 인터록 시간을 나타냄
            // transitionTo() 호출 시 stateTimer는 인터록 완료 직후 시점으로 정확히 설정됨
            Serial.println("[STATE] Opening Started. Toggling H-Bridge.");
            setRelayStates(true, true, false); // Main ON, Forward (A=ON, B=OFF)
            transitionTo(STATE_OPENING);
            break;
            
        case STATE_OPENING: {
            // OPEN_START에서 setRelayStates()로 릴레이가 이미 설정됨
            // 여기서는 스톨 감지 및 actuatorTime 타임아웃만 처리
            // Bypass motor inrush current for the first 500ms (dual actuator startup)
            if (hw.getMillis() - stateTimer > 500) {
                if (std::fabs(motorCurrent) > config.currentStallLimit) {
                    openStallCount++;
                    Serial.printf("[WARN] Stall candidate OPEN (%d/3): %.1f mA (Limit: %.1f mA)\n",
                                  openStallCount, motorCurrent, config.currentStallLimit);
                    if (openStallCount >= 3) {
                        Serial.println("[INFO] Motor reached physical OPEN limit (Stall detected).");
                        Serial.printf("[DIAGNOSTIC] Time Elapsed: %lu ms, Batt: %.2f V, Dist: %.1f cm\n",
                                      hw.getMillis() - stateTimer, batteryVoltage, currentDistance);
                        openStallCount = 0;
                        forceAllRelaysOff(); // 모터 정지
                        transitionTo(STATE_HOLD); // 강제 종료가 아닌 열림 유지 상태로 정상 전이
                        break;
                    }
                } else {
                    openStallCount = 0; // Current normal — reset counter
                }
            } else {
                openStallCount = 0; // Reset at inrush bypass window start
            }
            if (hw.getMillis() - stateTimer >= config.actuatorTime) {
                Serial.println("[STATE] Opening completed.");
                openStallCount = 0;
                setRelayStates(false, false, false);
                if (maintenanceRequested) {
                    transitionTo(STATE_MAINTENANCE);
                } else {
                    transitionTo(STATE_HOLD);
                }
            }
            break;
        }
            
        case STATE_HOLD:
            setRelayStates(false, false, false);
            
            if (hw.getMillis() - holdStartTime >= config.maxHoldTime) {
                Serial.println("[EMERGENCY] Sensor Deadlock detected (Max Hold Time exceeded). Forcing close!");
                sensorDeadlockFlag = true;
                transitionTo(STATE_CLOSE_START);
                break;
            }
            
            // If human is still detected within the threshold, reset the timer to keep the lid open
            if (!isCalibrationPhase && currentDistance > 0.0f && currentDistance < config.distThreshold) {
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
            if (hw.getMillis() - stateTimer > 500) {
                if (std::fabs(motorCurrent) > config.currentStallLimit) {
                    closeStallCount++;
                    Serial.printf("[WARN] Stall candidate CLOSE (%d/3): %.1f mA (Limit: %.1f mA)\n",
                                  closeStallCount, motorCurrent, config.currentStallLimit);
                    if (closeStallCount >= 3) {
                        Serial.printf("[WARN] MOTOR STALL DETECTED DURING CLOSE! Current: %.1f mA (Limit: %.1f mA)\n", motorCurrent, config.currentStallLimit);
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
                closeStallCount = 0; // Reset at inrush bypass window start
            }
            // Safety reopen if human is detected during close (bypassed if sensor is deadlocked or during calibration)
            if (!isCalibrationPhase && currentDistance > 0 && currentDistance < config.distThreshold && !sensorDeadlockFlag) {
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
                postCloseBlindStartTime = hw.getMillis(); // 블라인드 타임 시작 (기구물 안정화)
                Serial.println("[SENSOR] Post-close blind time started. Ignoring sensor for 3000ms.");
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
                if (currentDistance > 0.0f && currentDistance < config.distThreshold && !sensorDeadlockFlag) {
                    Serial.printf("[RECOVERY] Human detected (%.1f cm) → Auto-opening lid.\n", currentDistance);
                    transitionTo(STATE_OPEN_START);
                } else {
                    Serial.println("[RECOVERY] No human detected → Returning to IDLE.");
                    transitionTo(STATE_IDLE);
                }
            }
            break;
            
        case STATE_MAINTENANCE:
            setRelayStates(false, false, false);
            if (hw.getMillis() - stateTimer >= MAINTENANCE_TIMEOUT_MS) {
                Serial.println("[STATE] Maintenance mode timeout reached. Starting closing.");
                maintenanceRequested = false;
                transitionTo(STATE_CLOSE_START);
            }
            break;
            
        case STATE_BATTERY_LOW_SHUTDOWN:
            // After 3.8s, isolate all relays to high-impedance mode
            if (hw.getMillis() - stateTimer >= config.actuatorTime) {
                forceAllRelaysOff();
            }
            break;

        case STATE_OTA_UPDATING:
            // OTA 모드: 모든 릴레이 차단 상태 유지, 아무 동작도 하지 않음
            // update() 상단의 early return으로 여기까지 도달하지 않지만 방어적 코드
            forceAllRelaysOff();
            break;
    }
}

void SmartBoxController::forceOpen() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if(currentState==STATE_OPEN_START || currentState==STATE_OPENING)return;
    if (powerSnapshot().actuationAllowed && currentState != STATE_OTA_UPDATING &&
        currentState != STATE_BATTERY_LOW_SHUTDOWN && currentState != STATE_EMERGENCY_STOP) {
        sensorDeadlockFlag = false;
        isCooldown = false;
        deadlockClearStartTime = 0;
        transitionTo(STATE_OPEN_START);
    }
}

void SmartBoxController::forceClose() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if (powerSnapshot().actuationAllowed && currentState != STATE_OTA_UPDATING &&
        currentState != STATE_BATTERY_LOW_SHUTDOWN && currentState != STATE_EMERGENCY_STOP) {
        if (currentState != STATE_CLOSING && currentState != STATE_CLOSE_START) {
            maintenanceRequested = false;
            sensorDeadlockFlag = false;
            isCooldown = false;
            deadlockClearStartTime = 0;
            transitionTo(STATE_CLOSE_START);
        }
    }
}

void SmartBoxController::resetEmergency() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if (powerSnapshot().actuationAllowed && currentState == STATE_EMERGENCY_STOP) {
        currentState = STATE_IDLE;
        isCooldown = true;
        cooldownTimer = hw.getMillis();
        relaysIsolated = false;
        Serial.println("[SYSTEM] Emergency Lock released. Re-entering IDLE.");
    }
}

void SmartBoxController::transitionTo(State newState) {
    State prev;
    {
        std::lock_guard<std::recursive_mutex> lock(dataMutex);
        prev = currentState;
        currentState = newState;
        stateTimer = hw.getMillis();
        lastDetectStartTime = 0;
        if (newState == STATE_HOLD) {
            holdStartTime = hw.getMillis();
        }
        // BUG-02 fix: reset stall counters on every state transition
        openStallCount = 0;
        closeStallCount = 0;
        // FIX-2: reset one-shot relay flag on every state transition
        openingRelayStarted = false;
    }
    
    if (stateCallback != nullptr) {
        stateCallback(prev, newState);
    }
    
    Serial.printf("[STATE] Transition: %d -> %d\n", prev, newState);
}

void SmartBoxController::updateDistanceBuffer() {
    float raw = hw.getDistanceCm();
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    distanceHistory[distanceHistoryIdx] = raw;
    distanceHistoryIdx = (distanceHistoryIdx + 1) % 5;
}

float SmartBoxController::getFilteredDistance() {
    float sorted[5];
    {
        std::lock_guard<std::recursive_mutex> lock(dataMutex);
        for (int i = 0; i < 5; i++) {
            sorted[i] = distanceHistory[i];
        }
    }
    // Bubble sort
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (sorted[i] > sorted[j]) {
                float temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }
    return sorted[2];
}

void SmartBoxController::readSensors() {
    const bool previousValid=latestPowerRead.valid && std::isfinite(latestPowerRead.voltage) &&
                             std::isfinite(latestPowerRead.currentMa) && latestPowerRead.voltage>0 && latestPowerRead.voltage<=16;
    const auto reading=hw.readPower();
    const bool completeValid=reading.valid && std::isfinite(reading.voltage) &&
                             std::isfinite(reading.currentMa) && reading.voltage>0 && reading.voltage<=16;
    // Invalid sensor data remains diagnostic; no normal-motion stop or rearm.
    if(!completeValid && (previousValid || powerFaultEpisodes==0)) {
        lastPowerFaultRead=reading;lastPowerFaultRead.valid=false;
        if(lastPowerFaultRead.fault==PowerReadFault::NONE) {
            lastPowerFaultRead.fault=(!std::isfinite(reading.voltage) || !std::isfinite(reading.currentMa)) ?
                PowerReadFault::NONFINITE : PowerReadFault::RANGE;
            if(std::isfinite(reading.voltage) && (reading.voltage<=0 || reading.voltage>16))lastPowerFaultRead.rangeMask|=1;
        }
        lastPowerFaultAt=uint32_t(hw.getMillis());
        if(powerFaultEpisodes<UINT32_MAX)++powerFaultEpisodes;
    }
    latestPowerRead=reading;
    if(reading.valid && std::isfinite(reading.voltage) && std::isfinite(reading.currentMa))lastVerifiedVoltage=reading.voltage;
    batteryVoltage=reading.voltage; motorCurrent=reading.currentMa;
    batteryPolicy.sample(static_cast<uint32_t>(hw.getMillis()),reading.voltage,
                         reading.valid && std::isfinite(reading.currentMa),config.voltageShutdownLimit);
}
bool SmartBoxController::servicePower() {
    const auto before=powerSnapshot();
    const bool moving=isMotorRunning();
    const unsigned long period=before.protectedMode && !moving ? 1000 : 200;
    if(hw.getMillis()-lastPowerReading>=period) {
        lastPowerReading=hw.getMillis(); readSensors();
    }
    const auto after=powerSnapshot();
    if(chargingCloseActive) {
        const uint32_t elapsed=uint32_t(hw.getMillis()-chargingCloseAt);
        if(!after.valid) stopChargingClose(ChargingCloseState::SENSOR_FAULT);
        else if(!chargingClosePowerSafe()) stopChargingClose(ChargingCloseState::LOW_VOLTAGE);
        else if(currentState!=STATE_BATTERY_LOW_SHUTDOWN) stopChargingClose(ChargingCloseState::CANCELLED);
        else if(uint32_t(hw.getMillis()-chargingCloseRequestAt)>=chargingCloseTtl) stopChargingClose(ChargingCloseState::EXPIRED);
        else if(elapsed>=std::min(config.actuatorTime,3800UL)) stopChargingClose(ChargingCloseState::STROKE_FINISHED);
        else if(elapsed>500) {
            const float limit=std::isfinite(config.currentStallLimit) && config.currentStallLimit>0 ? config.currentStallLimit : 3000.0f;
            chargingCloseStalls=std::fabs(motorCurrent)>limit ? chargingCloseStalls+1 : 0;
            if(chargingCloseStalls>=3) stopChargingClose(ChargingCloseState::OVERCURRENT);
        }
        return true;
    }
    if(after.protectedMode) {
        if(currentState==STATE_OTA_UPDATING) {forceAllRelaysOff(); return true;}
        if(currentState!=STATE_BATTERY_LOW_SHUTDOWN) {
            const bool critical=after.valid && after.state==BatteryState::PROTECTED;
            lowBatteryOpening=critical;
            transitionTo(STATE_BATTERY_LOW_SHUTDOWN);
            if(critical) setRelayStates(true,true,false); // Existing one bounded fail-open stroke.
            else forceAllRelaysOff();
        }
        if(!after.valid) lowBatteryOpening=false;
        if(!lowBatteryOpening || hw.getMillis()-stateTimer>=config.actuatorTime) {
            lowBatteryOpening=false; forceAllRelaysOff();
        }
        return true;
    }
    if(currentState==STATE_BATTERY_LOW_SHUTDOWN) {
        forceAllRelaysOff(); lowBatteryOpening=false;
        maintenanceRequested=false; isCalibrationPhase=false; sensorDeadlockFlag=false;
        deadlockClearStartTime=0;
        isCooldown=false; lastDetectStartTime=0; distanceHistoryIdx=0;
        currentDistance=999.0f;
        for(float &distance:distanceHistory) distance=999.0f;
        postCloseBlindStartTime=hw.getMillis();
        transitionTo(STATE_IDLE);
        return true; // Never run an actuator on the recovery tick.
    }
    if(!after.actuationAllowed && !moving) return true;
    return false;
}
bool SmartBoxController::canStartOta(bool sensorFaultRecovery,bool forceMaintenance) const {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    const uint32_t now=static_cast<uint32_t>(hw.getMillis());const auto power=batteryPolicy.snapshot(now);
    if(sensorFaultRecovery && forceMaintenance)return false;
    if(forceMaintenance)return !otaRecoveryUsed && now>=120000 && !isMotorRunning() &&
        (currentState==STATE_IDLE || (currentState==STATE_BATTERY_LOW_SHUTDOWN && relaysIsolated)) &&
        power.forceOtaReady && std::isfinite(motorCurrent) && std::fabs(motorCurrent)<=50.0f;
    if(!sensorFaultRecovery)return currentState==STATE_IDLE && power.otaReady;
    const float cutoff=std::isfinite(config.voltageShutdownLimit) && config.voltageShutdownLimit>0 && config.voltageShutdownLimit<25 ? config.voltageShutdownLimit : BatteryPolicy::kDefaultCutoffVoltage;
    // Explicit owner recovery only; unknown battery capacity is not a healthy-voltage claim.
    return !otaRecoveryUsed && now>=120000 && (currentState==STATE_IDLE || currentState==STATE_BATTERY_LOW_SHUTDOWN) &&
        !isMotorRunning() && power.state==BatteryState::SENSOR_FAULT &&
        power.sampleAgeMs<=2000 && (!std::isfinite(lastVerifiedVoltage) || (lastVerifiedVoltage>cutoff && lastVerifiedVoltage<=16.0f));
}
bool SmartBoxController::tryBeginOta(bool sensorFaultRecovery,bool forceMaintenance) {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if(!canStartOta(sensorFaultRecovery,forceMaintenance))return false;
    forceAllRelaysOff();otaRecoveryReserved=sensorFaultRecovery;otaMaintenanceReserved=forceMaintenance;
    if(sensorFaultRecovery || forceMaintenance){otaRecoveryUsed=true;otaRecoveryAt=static_cast<uint32_t>(hw.getMillis());}
    transitionTo(STATE_OTA_UPDATING);return true;
}
bool SmartBoxController::canContinueOta() const {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if(currentState!=STATE_OTA_UPDATING || !relaysIsolated || isMotorRunning())return false;
    const uint32_t now=static_cast<uint32_t>(hw.getMillis());const auto power=batteryPolicy.snapshot(now);
    if(otaMaintenanceReserved)return uint32_t(now-otaRecoveryAt)<=120000 && power.forceOtaReady &&
        std::isfinite(motorCurrent) && std::fabs(motorCurrent)<=50.0f;
    if(!otaRecoveryReserved)return power.otaReady;
    const float cutoff=std::isfinite(config.voltageShutdownLimit) && config.voltageShutdownLimit>0 && config.voltageShutdownLimit<25 ? config.voltageShutdownLimit : BatteryPolicy::kDefaultCutoffVoltage;
    return uint32_t(now-otaRecoveryAt)<=120000 && power.sampleAgeMs<=2000 &&
        (!std::isfinite(lastVerifiedVoltage) || (lastVerifiedVoltage>cutoff && lastVerifiedVoltage<=16.0f));
}
void SmartBoxController::servicePowerDuringOta() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    servicePower();
}
void SmartBoxController::finishOtaWithoutReboot() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    otaRecoveryReserved=false;otaMaintenanceReserved=false;
    forceAllRelaysOff(); lowBatteryOpening=false;
    const auto power=powerSnapshot();
    if(power.persistentHold) batteryPolicy.restoreProtection();
    transitionTo(power.persistentHold ? STATE_BATTERY_LOW_SHUTDOWN : STATE_IDLE);
    postCloseBlindStartTime=hw.getMillis();
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

    // 멤버 변수로 관리 (static local 제거: 테스트 간 오염 방지)
    bool dirChanged = (dirOpen != lastDirOpen) || (dirClose != lastDirClose) ||
                      (mainOn && relaysIsolated);
    
    if (dirChanged) {
        // Interlock Guard 2: Turn off main relay and wait for contact release (100ms)
        hw.setPinMode(RELAY_MAIN_PIN, INPUT);
        hw.delayMs(100);
        
        // Preload neutral before restoring drive after high-impedance isolation.
        hw.writePin(RELAY_DIR_A_PIN, HIGH);
        hw.writePin(RELAY_DIR_B_PIN, HIGH);
        if (mainOn) {
            hw.setPinMode(RELAY_DIR_A_PIN, OUTPUT);
            hw.setPinMode(RELAY_DIR_B_PIN, OUTPUT);
        }
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
    
    // Re-read after both contact delays: a closing recovery must never energize
    // from admission evidence captured before the contacts separated.
    if(mainOn && chargingCloseActive) {
        lastPowerReading=hw.getMillis();readSensors();
        if(!powerSnapshot().valid) {stopChargingClose(ChargingCloseState::SENSOR_FAULT);return;}
        if(!chargingClosePowerSafe()) {stopChargingClose(ChargingCloseState::LOW_VOLTAGE);return;}
        if(uint32_t(hw.getMillis()-chargingCloseRequestAt)>=chargingCloseTtl) {stopChargingClose(ChargingCloseState::EXPIRED);return;}
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
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    hw.setPinMode(RELAY_MAIN_PIN, INPUT);
    hw.setPinMode(RELAY_DIR_A_PIN, INPUT);
    hw.setPinMode(RELAY_DIR_B_PIN, INPUT);
    if (!relaysIsolated) {
        relaysIsolated = true;
        Serial.println("[SAFETY] Relay outputs isolated; total standby draw is not measured.");
    }
}

bool SmartBoxController::isMotorRunning() const {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if(chargingCloseActive)return true;
    if (currentState == STATE_OPEN_START || currentState == STATE_OPENING ||
        currentState == STATE_CLOSE_START || currentState == STATE_CLOSING) {
        return true;
    }
    if (currentState == STATE_BATTERY_LOW_SHUTDOWN) {
        return lowBatteryOpening && (hw.getMillis() - stateTimer < config.actuatorTime);
    }
    return false;
}

bool SmartBoxController::canSendTelemetry() const {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if (currentState == STATE_OTA_UPDATING) {
        return false;
    }
    if (isMotorRunning()) {
        return false;
    }
    return (currentState == STATE_IDLE || 
            currentState == STATE_HOLD || 
            currentState == STATE_EMERGENCY_STOP || 
            currentState == STATE_STARTUP_OPEN || 
            currentState == STATE_MAINTENANCE ||
            (currentState == STATE_BATTERY_LOW_SHUTDOWN && !isMotorRunning()));
}

void SmartBoxController::startMaintenanceMode() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if (!powerSnapshot().actuationAllowed || currentState == STATE_BATTERY_LOW_SHUTDOWN || currentState == STATE_EMERGENCY_STOP || currentState == STATE_OTA_UPDATING) {
        return;
    }
    maintenanceRequested = true;
    if (currentState == STATE_HOLD || currentState == STATE_STARTUP_OPEN) {
        transitionTo(STATE_MAINTENANCE);
    } else if (currentState != STATE_MAINTENANCE && currentState != STATE_OPENING && currentState != STATE_OPEN_START) {
        transitionTo(STATE_OPEN_START);
    }
}

void SmartBoxController::stopMaintenanceMode() {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if(currentState==STATE_OTA_UPDATING) return;
    maintenanceRequested = false;
    if (currentState == STATE_MAINTENANCE) {
        transitionTo(STATE_CLOSE_START);
    }
}

unsigned long SmartBoxController::getMaintenanceRemainingSeconds() const {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if (currentState != STATE_MAINTENANCE) {
        return 0;
    }
    unsigned long elapsed = hw.getMillis() - stateTimer;
    if (elapsed >= MAINTENANCE_TIMEOUT_MS) {
        return 0;
    }
    return (MAINTENANCE_TIMEOUT_MS - elapsed) / 1000;
}



bool SmartBoxController::chargingClosePowerSafe() const {
    const auto power=powerSnapshot();
    const float cutoff=std::isfinite(config.voltageShutdownLimit) ? std::max(BatteryPolicy::kDefaultCutoffVoltage,config.voltageShutdownLimit) : BatteryPolicy::kDefaultCutoffVoltage;
    return power.valid && std::isfinite(motorCurrent) && power.voltage>cutoff && power.voltage<=16.0f;
}
void SmartBoxController::stopChargingClose(ChargingCloseState reason) {
    forceAllRelaysOff();chargingCloseActive=false;lowBatteryOpening=false;
    chargingCloseResult=reason;batteryPolicy.restoreProtection();
    Serial.printf("[POWER] Charging close stopped result=%u; protection retained; physical lid unconfirmed.\n",unsigned(reason));
}
bool SmartBoxController::requestChargingClose(uint32_t ttlMs) {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);
    if(chargingCloseUsed || ttlMs==0 || ttlMs>120000 || currentState!=STATE_BATTERY_LOW_SHUTDOWN || isMotorRunning() || config.actuatorTime==0)return false;
    lastPowerReading=hw.getMillis();readSensors();
    if(!chargingClosePowerSafe() || !powerSnapshot().protectedMode || std::fabs(motorCurrent)>50.0f)return false;
    chargingCloseUsed=true;chargingCloseActive=true;chargingCloseResult=ChargingCloseState::RUNNING;
    chargingCloseRequestAt=uint32_t(hw.getMillis());chargingCloseTtl=ttlMs;chargingCloseStalls=0;
    lowBatteryOpening=false;maintenanceRequested=false;isCalibrationPhase=false;
    batteryPolicy.restoreProtection();
    setRelayStates(true,false,true);
    if(!chargingCloseActive)return false;
    chargingCloseAt=uint32_t(hw.getMillis());
    Serial.println("[POWER] Explicit one-shot charging close started; protection stays latched.");
    return true;
}
ChargingCloseState SmartBoxController::chargingCloseState() const {
    std::lock_guard<std::recursive_mutex> lock(dataMutex);return chargingCloseResult;
}
