#include "RecoveryOtaRequest.h"
#ifndef NATIVE_BUILD
#include <Arduino.h>
#endif
#include <unity.h>
#include <cmath>
#ifdef NATIVE_BUILD
#include <future>
#include <condition_variable>
#include <thread>
#endif
#include "MockHardware.h"

#include "SmartBoxController.h"
#include "Ina219Sampler.h"

#ifdef NATIVE_BUILD
MockSerial Serial;
#endif

// Pin definitions
static const int RELAY_MAIN_PIN = 6;
static const int RELAY_DIR_A_PIN = 7;
static const int RELAY_DIR_B_PIN = 8;

void setUp(void) {
    // Run before each test
}

void tearDown(void) {
    // Run after each test
}

void test_relay_glitch_protection(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    
    // Before initialization, all pins should default to INPUT (high impedance)
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_B_PIN));
    
    controller.init();
    
    // After initialization, to prevent boot glitches:
    // - RELAY_MAIN_PIN should remain in INPUT mode (or high impedance)
    // - Direction relays must be set to OUTPUT and written HIGH (inactive)
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(OUTPUT, hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(OUTPUT, hw.getPinMode(RELAY_DIR_B_PIN));
    TEST_ASSERT_EQUAL(HIGH, hw.getPinValue(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(HIGH, hw.getPinValue(RELAY_DIR_B_PIN));
}

void test_relay_interlock_and_guard_delay(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // Reset any init-time logs
    hw.clearEventHistory();
    
    // Simulate a human approaching (30cm) to trigger STATE_IDLE -> STATE_OPEN_START -> STATE_OPENING
    hw.setDistanceCm(30.0f);
    
    // Run update 15 times with 50ms intervals to populate buffer and pass 300ms debounce
    for (int i = 0; i < 15; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    
    const std::vector<PinEvent>& history = hw.getEventHistory();
    
    // Interlock Mutex Check: DIR_A and DIR_B must never be LOW (ON) at the same time
    bool dirAActive = false;
    bool dirBActive = false;
    for (const auto& ev : history) {
        if (ev.type == EVENT_WRITE) {
            if (ev.pin == RELAY_DIR_A_PIN) {
                dirAActive = (ev.value == LOW);
            }
            if (ev.pin == RELAY_DIR_B_PIN) {
                dirBActive = (ev.value == LOW);
            }
            TEST_ASSERT_FALSE_MESSAGE(dirAActive && dirBActive, "Relay Interlock Violated: Both direction pins are LOW simultaneously!");
        }
    }
    
    // Timeline Check
    int mainOffIndex = -1;
    int dirChangeIndex = -1;
    int mainOnIndex = -1;
    
    for (size_t i = 0; i < history.size(); ++i) {
        const auto& ev = history[i];
        if (ev.type == EVENT_MODE && ev.pin == RELAY_MAIN_PIN && ev.value == INPUT) {
            mainOffIndex = i;
        } else if (mainOffIndex != -1 && ev.type == EVENT_WRITE && ev.pin == RELAY_DIR_A_PIN && ev.value == LOW) {
            dirChangeIndex = i;
        } else if (dirChangeIndex != -1 && ev.type == EVENT_MODE && ev.pin == RELAY_MAIN_PIN && ev.value == OUTPUT) {
            mainOnIndex = i;
        }
    }
    
    TEST_ASSERT_MESSAGE(mainOffIndex != -1, "Main relay was never cut off (INPUT mode)!");
    TEST_ASSERT_MESSAGE(dirChangeIndex != -1, "Direction was never changed (DIR_A -> LOW)!");
    TEST_ASSERT_MESSAGE(mainOnIndex != -1, "Main relay was never turned back ON (OUTPUT mode)!");
    
    // Verify timing gaps (must be >= 100ms)
    unsigned long tMainOff = history[mainOffIndex].timestamp;
    unsigned long tDirChange = history[dirChangeIndex].timestamp;
    unsigned long tMainOn = history[mainOnIndex].timestamp;
    
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(100, tDirChange - tMainOff, "Interlock gap before direction change is less than 100ms!");
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(100, tMainOn - tDirChange, "Interlock gap after direction change is less than 100ms!");
}

void test_stall_current_detection(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // Trigger opening transition (must satisfy debounce)
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 15; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Advance virtual clock by 150ms (within 500ms inrush window: total 400ms elapsed since entering STATE_OPENING)
    hw.advanceMillis(150);
    hw.setMotorCurrent(4000.0f); // Exceeds 3000mA stall threshold
    controller.update();
    
    // Should still be OPENING (inrush bypass)
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Advance virtual clock by 150ms (total 550ms since opening, past the 500ms window)
    hw.advanceMillis(150);
    
    // Sample 1: exceeding threshold
    hw.setMotorCurrent(4000.0f);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState()); // Needs 3 consecutive samples
    
    // Sample 2: exceeding threshold
    hw.setMotorCurrent(4000.0f);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Sample 3: exceeding threshold -> MUST transition to STATE_HOLD (normal physical limit reached during open)
    hw.setMotorCurrent(4000.0f);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());
    
    // All relays must be isolated to INPUT (high-impedance) for safety
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_B_PIN));
}

void test_low_battery_shutdown(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // Simulate low battery (11.0V, under 11.3V threshold)
    hw.setBatteryVoltage(11.0f);
    
    // Run update for 2 seconds (sensor reads every 200ms).
    // FSM should NOT shutdown yet (threshold is 3 seconds)
    for (int i = 0; i < 10; ++i) {
        hw.advanceMillis(200);
        controller.update();
    }
    TEST_ASSERT_NOT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN, controller.getCurrentState());
    
    // Advance virtual clock by another 1.6 seconds (total 3.6 seconds of continuous low battery)
    for (int i = 0; i < 8; ++i) {
        hw.advanceMillis(200);
        controller.update();
    }
    
    // FSM must now transition to STATE_BATTERY_LOW_SHUTDOWN
    TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN, controller.getCurrentState());
    
    // On transition, it must initiate lid opening (Main=ON, Forward A=ON)
    TEST_ASSERT_EQUAL(OUTPUT, hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(OUTPUT, hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(LOW, hw.getPinValue(RELAY_MAIN_PIN)); // Active-Low ON
    TEST_ASSERT_EQUAL(LOW, hw.getPinValue(RELAY_DIR_A_PIN)); // Active-Low ON
    
    // Advance clock past the actuator stroke time (3.8 seconds)
    hw.advanceMillis(3800);
    controller.update();
    
    // After 3.8s, all relays must be forced to INPUT (high impedance) to prevent battery drain
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_B_PIN));
}

void test_ultrasonic_median_filter(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // Noise Test 1: mostly far, with 2 close spikes
    // Input sequence: 10cm, 200cm, 10cm, 200cm, 200cm -> Sorted: 10, 10, 200, 200, 200 -> Median: 200cm
    // FSM must remain IDLE.
    float inputs1[5] = {10.0f, 200.0f, 10.0f, 200.0f, 200.0f};
    for (int i = 0; i < 5; ++i) {
        hw.setDistanceCm(inputs1[i]);
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_IDLE, controller.getCurrentState());
    
    // Noise Test 2: mostly close, with 2 sensor error (999cm) spikes
    // Input sequence: 999cm, 30cm, 999cm, 30cm, 30cm -> Sorted: 30, 30, 30, 999, 999 -> Median: 30cm
    float inputs2[5] = {999.0f, 30.0f, 999.0f, 30.0f, 30.0f};
    for (int i = 0; i < 5; ++i) {
        hw.setDistanceCm(inputs2[i]);
        hw.advanceMillis(50);
        controller.update();
    }
    // With 300ms debounce, after 5 updates (only 50ms since median became 30cm), it must still be IDLE
    TEST_ASSERT_EQUAL(STATE_IDLE, controller.getCurrentState());
    
    // Continue close detection for another 6 updates (total 300ms additional, total 350ms continuous)
    for (int i = 0; i < 6; ++i) {
        hw.setDistanceCm(30.0f);
        hw.advanceMillis(50);
        controller.update();
    }
    
    // FSM must now transition to STATE_OPEN_START
    TEST_ASSERT_EQUAL(STATE_OPEN_START, controller.getCurrentState());
    
    // One more update to transition from OPEN_START to OPENING
    hw.advanceMillis(10);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
}

static State gPrevState = STATE_IDLE;
static State gNewState = STATE_IDLE;
static int gCallbackCount = 0;

static void testStateCallback(State prev, State next) {
    gPrevState = prev;
    gNewState = next;
    gCallbackCount++;
}

void test_config_and_callback_trigger(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // Register test callback
    gCallbackCount = 0;
    controller.registerStateCallback(testStateCallback);
    
    // Modify config to increase range threshold to 100cm
    BoxConfig cfg = controller.getConfig();
    cfg.distThreshold = 100.0f;
    controller.setConfig(cfg);
    
    // Proximity target at 80cm (outside default 50cm, inside new 100cm)
    hw.setDistanceCm(80.0f);
    for (int i = 0; i < 15; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    
    // State machine should transition to STATE_OPENING
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Callback must be triggered exactly twice (IDLE -> OPEN_START -> OPENING)
    TEST_ASSERT_EQUAL(2, gCallbackCount);
    TEST_ASSERT_EQUAL(STATE_OPEN_START, gPrevState);
    TEST_ASSERT_EQUAL(STATE_OPENING, gNewState);
}

void test_hold_duration_and_retrigger(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // 1. Trigger open: Human at 30cm (within threshold)
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 15; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // 2. Advance by actuatorTime to reach STATE_HOLD
    hw.advanceMillis(3800);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());
    
    // 3. Human moves away (80cm)
    hw.setDistanceCm(80.0f);
    
    // Advance 5 seconds. Since we removed early-close, it should remain in STATE_HOLD.
    for (int i = 0; i < 100; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());
    
    // 4. Human re-approaches (30cm) to trigger timer reset (must update 5 times to populate median filter)
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 5; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    
    // 5. Human moves away again (80cm)
    hw.setDistanceCm(80.0f);
    
    // Advance 8 seconds. Should still be in STATE_HOLD.
    for (int i = 0; i < 160; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());
    
    // Advance another 1.9 seconds (total 9.9 seconds since reset).
    // Should still be in STATE_HOLD.
    for (int i = 0; i < 38; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());
    
    // 6. Advance another 250ms to cross the 10-second threshold (total 10.05 seconds since actual timer stop).
    hw.advanceMillis(250);
    
    // First update must transition from STATE_HOLD to STATE_CLOSE_START
    controller.update();
    TEST_ASSERT_EQUAL(STATE_CLOSE_START, controller.getCurrentState());
    
    // Next update must transition from STATE_CLOSE_START to STATE_CLOSING
    controller.update();
    TEST_ASSERT_EQUAL(STATE_CLOSING, controller.getCurrentState());
}

void test_startup_open_flow(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    
    // Set initial state to STATE_STARTUP_OPEN
    controller.setInitialState(STATE_STARTUP_OPEN);
    controller.init();
    
    // Check initial state
    TEST_ASSERT_EQUAL(STATE_STARTUP_OPEN, controller.getCurrentState());
    
    // Relays must be isolated/standby (Main = INPUT, Directions = OUTPUT/HIGH)
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(OUTPUT, hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(OUTPUT, hw.getPinMode(RELAY_DIR_B_PIN));
    TEST_ASSERT_EQUAL(HIGH, hw.getPinValue(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(HIGH, hw.getPinValue(RELAY_DIR_B_PIN));
    
    // [FIX-1] STARTUP_OPEN now homes to CLOSE_START after 500ms stabilization,
    // regardless of human detection. Simulate 400ms (< 500ms): must stay in STARTUP_OPEN.
    hw.setDistanceCm(30.0f); // Human present — should no longer trigger HOLD
    for (int i = 0; i < 8; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    // 400ms elapsed — still in STARTUP_OPEN (500ms not yet reached)
    TEST_ASSERT_EQUAL(STATE_STARTUP_OPEN, controller.getCurrentState());
    
    // Advance past 500ms threshold (2 more ticks = 100ms → total 500ms)
    for (int i = 0; i < 2; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    // 500ms elapsed — must transition to OPEN_START (calibration sequence)
    TEST_ASSERT_EQUAL(STATE_OPEN_START, controller.getCurrentState());
    
    // One more update: OPEN_START -> OPENING
    hw.advanceMillis(50);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Relays should be active during OPENING (Main=OUTPUT/LOW, DIR_A=LOW)
    TEST_ASSERT_EQUAL(OUTPUT, hw.getPinMode(RELAY_MAIN_PIN));
}


void test_ota_state_isolation(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // 1. Trigger opening first (simulate mid-operation OTA trigger)
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 15; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // 2. Simulate Pre-OTA interlock: forceAllRelaysOff() then transitionTo(STATE_OTA_UPDATING)
    controller.forceAllRelaysOff();
    
    // All relay pins must be INPUT (high-impedance, 0mA standby)
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_B_PIN));
    
    controller.transitionTo(STATE_OTA_UPDATING);
    TEST_ASSERT_EQUAL(STATE_OTA_UPDATING, controller.getCurrentState());
    
    // 3. Verify FSM is frozen: sensor stimulus should NOT cause state transitions
    hw.setDistanceCm(10.0f); // Very close approach
    hw.setBatteryVoltage(12.0f); // Normal battery
    for (int i = 0; i < 20; ++i) {
        hw.advanceMillis(200);
        controller.update();
    }
    
    // State must remain STATE_OTA_UPDATING despite sensor activity
    TEST_ASSERT_EQUAL(STATE_OTA_UPDATING, controller.getCurrentState());
    
    // All relay pins must still be INPUT (no relay activity during OTA)
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_DIR_B_PIN));
    
    // 4. Verify isOtaMode() returns true
    TEST_ASSERT_TRUE(controller.isOtaMode());
}


void test_maintenance_mode(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // 1. Trigger maintenance mode from IDLE
    controller.startMaintenanceMode();
    
    // FSM should immediately transition to STATE_OPEN_START
    TEST_ASSERT_EQUAL(STATE_OPEN_START, controller.getCurrentState());
    
    // Running update should transition to STATE_OPENING
    controller.update();
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Verify that while opening, if we receive close range sensor triggers, it does NOT abort or change behavior
    hw.setDistanceCm(10.0f);
    hw.advanceMillis(500); // Past startup current inrush bypass (500ms)
    controller.update();
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Advance remaining actuator time to complete opening
    // config.actuatorTime defaults to 3800. We already advanced 500ms, so let's advance 3300ms more.
    hw.advanceMillis(3300);
    controller.update();
    
    // Once opening completes, FSM must transition to STATE_MAINTENANCE (since maintenanceRequested is true)
    TEST_ASSERT_EQUAL(STATE_MAINTENANCE, controller.getCurrentState());
    
    // Relays should be fully isolated/standby (Main = INPUT)
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
    
    // 2. Verify that distance sensors are ignored while in maintenance state
    for (int i = 0; i < 20; ++i) {
        hw.setDistanceCm(10.0f); // Close
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_MAINTENANCE, controller.getCurrentState());
    
    // 3. Verify that 300,000ms (5 minutes) timeout triggers auto-close
    // In step 2, we advanced 20 * 50 = 1000ms.
    // Let's advance another 299,000ms (total 300,000ms elapsed since entering STATE_MAINTENANCE)
    hw.advanceMillis(299000);
    controller.update();
    
    // At exactly 300,000ms, it should transition to STATE_CLOSE_START
    TEST_ASSERT_EQUAL(STATE_CLOSE_START, controller.getCurrentState());
    
    // Run update to transition to STATE_CLOSING
    controller.update();
    TEST_ASSERT_EQUAL(STATE_CLOSING, controller.getCurrentState());
    
    // 4. Test manual stop / override
    // Reset controller and go to maintenance state again
    controller.init();
    controller.startMaintenanceMode();
    controller.update(); // IDLE -> OPEN_START -> OPENING
    hw.advanceMillis(3800);
    controller.update(); // OPENING -> MAINTENANCE
    TEST_ASSERT_EQUAL(STATE_MAINTENANCE, controller.getCurrentState());
    
    // Now stop maintenance mode manually
    controller.stopMaintenanceMode();
    
    // FSM must transition directly to STATE_CLOSE_START
    TEST_ASSERT_EQUAL(STATE_CLOSE_START, controller.getCurrentState());
    
    // Transition to STATE_CLOSING
    controller.update();
    TEST_ASSERT_EQUAL(STATE_CLOSING, controller.getCurrentState());
}

void test_telemetry_interval_config(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // Check default
    BoxConfig cfg = controller.getConfig();
    TEST_ASSERT_EQUAL(10, cfg.telemetryIntervalMin);
    
    // Modify config
    cfg.telemetryIntervalMin = 15;
    controller.setConfig(cfg);
    
    BoxConfig updatedCfg = controller.getConfig();
    TEST_ASSERT_EQUAL(15, updatedCfg.telemetryIntervalMin);
}

void test_sensor_deadlock_prevention(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();
    
    // 1. Trigger open first (simulate human approach)
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 15; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Complete opening to enter STATE_HOLD
    hw.advanceMillis(3800);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());
    
    // Verify that the deadlock flag is initially false
    TEST_ASSERT_FALSE(controller.getSensorDeadlockFlag());
    
    // Simulate persistent human presence (sensor stays at 30cm)
    // Advance clock by 179 seconds (just under 180 seconds maxHoldTime)
    for (int i = 0; i < 179; ++i) {
        hw.advanceMillis(1000);
        controller.update();
    }
    // Should still be in STATE_HOLD
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());
    TEST_ASSERT_FALSE(controller.getSensorDeadlockFlag());
    
    // Advance by 2 more seconds (crossing the 180s threshold)
    hw.advanceMillis(2000);
    controller.update(); // FSM should transition to STATE_CLOSE_START
    TEST_ASSERT_EQUAL(STATE_CLOSE_START, controller.getCurrentState());
    TEST_ASSERT_TRUE(controller.getSensorDeadlockFlag());
    
    // Transition to STATE_CLOSING
    controller.update();
    TEST_ASSERT_EQUAL(STATE_CLOSING, controller.getCurrentState());
    
    // Complete closing to enter STATE_IDLE (actuatorTime default is 3800ms)
    hw.advanceMillis(3800);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_IDLE, controller.getCurrentState());
    
    // 2. Anti-Bounce Back: Sensor is still blocked (30cm), but FSM must NOT re-open
    // Run update for 20 ticks (1 second) and check that state remains IDLE
    for (int i = 0; i < 20; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_IDLE, controller.getCurrentState());
    TEST_ASSERT_TRUE(controller.getSensorDeadlockFlag());
    
    // 3. Self-healing: clear path (sensor at 80cm)
    //    NEW LOGIC: flag must NOT clear until path is clean for 10 CONTINUOUS seconds.
    hw.setDistanceCm(80.0f);
    
    // After only 5 updates (~250ms), deadlock flag must still be SET (not enough time)
    for (int i = 0; i < 5; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_TRUE(controller.getSensorDeadlockFlag()); // flag must persist
    
    // Advance 9 more seconds (total ~9.25s clean) - still should NOT be cleared
    for (int i = 0; i < 180; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_TRUE(controller.getSensorDeadlockFlag()); // still not cleared
    
    // Advance 1 more second (total > 10s clean) - NOW the flag must be cleared
    for (int i = 0; i < 20; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_FALSE(controller.getSensorDeadlockFlag()); // flag must be cleared after 10s
    
    // Advance remaining cooldown time to clear the cooldown state
    hw.advanceMillis(2000);
    controller.update();
    
    // 4. Verify FSM can open again now
    hw.setDistanceCm(30.0f); // Re-approach
    for (int i = 0; i < 15; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
}

void test_post_close_blind_time(void) {
    MockHardware hw;
    SmartBoxController controller(hw);
    controller.init();

    // 1. Trigger open (30cm, 15 ticks * 50ms = 750ms > debounceDelay 300ms)
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 15; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());

    // 2. Complete opening -> STATE_HOLD
    hw.advanceMillis(3800);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());

    // 3. Human moves away (80cm) -> closing starts after waitTime (10s)
    //    NOTE: median filter takes ~2 ticks (100ms) to shift from 30cm to 80cm,
    //    during which stateTimer is reset.
    //    - Tick 202: HOLD -> STATE_CLOSE_START
    //    - Tick 203: STATE_CLOSE_START -> STATE_CLOSING (immediate in same loop)
    //    - Ticks 204-205: STATE_CLOSING
    //    So after 205 ticks the FSM is in STATE_CLOSING, not STATE_CLOSE_START.
    hw.setDistanceCm(80.0f);
    for (int i = 0; i < 205; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_CLOSING, controller.getCurrentState()); // already past CLOSE_START

    // 4. Complete closing -> STATE_IDLE (postCloseBlindStartTime set here)
    //    Sensor stays at 80cm during CLOSING to avoid triggering Safety Reopen logic.
    //    Set to 30cm AFTER STATE_IDLE is entered to simulate lid bounce.
    // (already in STATE_CLOSING — no extra update() needed)
    hw.advanceMillis(3800);  // actuatorTime elapsed (stateTimer was reset at tick 203 entry)
    controller.update();     // CLOSING -> STATE_IDLE
    TEST_ASSERT_EQUAL(STATE_IDLE, controller.getCurrentState());


    // Now simulate lid bounce (sensor reads 30cm due to mechanical oscillation)
    hw.setDistanceCm(30.0f);

    // 5. BLIND TIME: sensor at 30cm (lid bounce), must NOT re-open within 3s
    for (int i = 0; i < 58; ++i) { // 2.9s (58 * 50ms)
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_IDLE, controller.getCurrentState()); // still blocked

    // 6. AFTER BLIND TIME: advance past 3s, then verify normal re-open works
    hw.advanceMillis(200); // total ~3.1s -> blind time expired
    controller.update();
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 15; ++i) { // 750ms > debounceDelay(300ms)
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState()); // re-open OK after blind time
}



void test_battery_recovers_only_after_continuous_safe_voltage(void) {
    MockHardware hw;
    SmartBoxController box(hw);
    box.init();
    hw.setBatteryVoltage(11.0f);
    for (int i=0;i<40;i++) { hw.advanceMillis(200); box.update(); }
    TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN,box.getCurrentState());
    hw.setBatteryVoltage(12.6f);
    for (int i=0;i<299;i++) { hw.advanceMillis(1000); box.update(); }
    TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN,box.getCurrentState());
    for (int i=0;i<3;i++) { hw.advanceMillis(1000); box.update(); }
    TEST_ASSERT_EQUAL(STATE_IDLE,box.getCurrentState());
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
}
void test_battery_recovery_dip_restarts_stable_window(void) {
    MockHardware hw; SmartBoxController box(hw); box.init();
    hw.setBatteryVoltage(11.0f);
    for(int i=0;i<40;i++){hw.advanceMillis(200);box.update();}
    hw.setBatteryVoltage(12.6f);
    for(int i=0;i<200;i++){hw.advanceMillis(1000);box.update();}
    hw.setBatteryVoltage(12.3f); hw.advanceMillis(1000); box.update();
    hw.setBatteryVoltage(12.6f);
    for(int i=0;i<150;i++){hw.advanceMillis(1000);box.update();}
    TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN,box.getCurrentState());
    for(int i=0;i<152;i++){hw.advanceMillis(1000);box.update();}
    TEST_ASSERT_EQUAL(STATE_IDLE,box.getCurrentState());
}
void test_bad_voltage_never_operates_motor(void) {
    MockHardware hw; hw.setBatteryVoltage(NAN);
    SmartBoxController box(hw);box.init();
    hw.advanceMillis(200);box.update();box.forceOpen();box.update();
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN,box.getCurrentState());
}


void test_sensor_failure_during_protective_stroke_stops_now(void) {
    MockHardware hw; SmartBoxController box(hw);box.init();
    hw.setBatteryVoltage(11.0f);
    for(int i=0;i<17;i++){hw.advanceMillis(200);box.update();}
    TEST_ASSERT_EQUAL(OUTPUT,hw.getPinMode(RELAY_MAIN_PIN));
    hw.setBatteryVoltage(NAN);hw.advanceMillis(200);box.update();
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
}
void test_restored_protection_cannot_repeat_emergency_stroke(void) {
    MockHardware hw;hw.setBatteryVoltage(11.0f);SmartBoxController box(hw);
    box.setInitialState(STATE_BATTERY_LOW_SHUTDOWN);box.init();hw.clearEventHistory();
    for(int i=0;i<40;i++){hw.advanceMillis(200);box.update();}
    for(const auto &event:hw.getEventHistory())
        TEST_ASSERT_FALSE(event.type==EVENT_MODE && event.pin==RELAY_MAIN_PIN && event.value==OUTPUT);
    TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN,box.getCurrentState());
}
void test_recovery_gap_cannot_count_as_continuous_charge(void) {
    BatteryPolicy policy;policy.restoreProtection();policy.sample(100,12.6f,true,11.3f);
    policy.sample(300101,12.6f,true,11.3f);
    TEST_ASSERT_TRUE(policy.snapshot(300101).protectedMode);
    TEST_ASSERT_EQUAL_UINT32(BatteryPolicy::kRecoveryMs,policy.snapshot(300101).recoveryRemainingMs);
}
void test_policy_recovers_across_u32_wrap(void) {
    BatteryPolicy policy;policy.restoreProtection();uint32_t now=UINT32_MAX-10000;
    for(int i=0;i<=300;i++){policy.sample(now,12.6f,true,11.3f);now+=1000;}
    TEST_ASSERT_FALSE(policy.snapshot(now-1000).protectedMode);
    TEST_ASSERT_EQUAL_UINT32(1,policy.snapshot(now-1000).recoveryCount);
}
void test_expired_measurement_denies_commands_and_ota(void) {
    MockHardware hw;hw.setBatteryVoltage(12.6f);SmartBoxController box(hw);box.init();
    hw.advanceMillis(2500);box.forceOpen();
    TEST_ASSERT_EQUAL(STATE_IDLE,box.getCurrentState());TEST_ASSERT_FALSE(box.canStartOta());
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
}
void test_low_timers_do_not_cross_controller_instances(void) {
    MockHardware a,b;SmartBoxController first(a),second(b);first.init();second.init();
    a.setBatteryVoltage(11.0f);
    for(int i=0;i<15;i++){a.advanceMillis(200);first.update();}
    b.setBatteryVoltage(11.0f);b.advanceMillis(200);second.update();
    TEST_ASSERT_EQUAL(STATE_IDLE,second.getCurrentState());
}
void test_ota_failed_recovery_cannot_clear_low_power_latch(void) {
    MockHardware hw;hw.setBatteryVoltage(12.6f);SmartBoxController box(hw);box.init();
    box.transitionTo(STATE_OTA_UPDATING);hw.setBatteryVoltage(NAN);
    hw.advanceMillis(200);box.servicePowerDuringOta();box.forceOpen();
    TEST_ASSERT_EQUAL(STATE_OTA_UPDATING,box.getCurrentState());
    box.finishOtaWithoutReboot();
    TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN,box.getCurrentState());
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
}


void test_subthreshold_grace_does_not_start_normal_motor(void) {
    MockHardware hw;hw.setBatteryVoltage(11.0f);hw.setDistanceCm(30);
    SmartBoxController box(hw);box.init();
    for(int i=0;i<20;i++){hw.advanceMillis(50);box.update();}
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
    TEST_ASSERT_EQUAL(STATE_IDLE,box.getCurrentState());
}
void test_power_fault_cannot_be_cleared_by_emergency_reset(void) {
    MockHardware hw;SmartBoxController box(hw);box.init();
    box.transitionTo(STATE_EMERGENCY_STOP);hw.advanceMillis(3000);box.resetEmergency();
    TEST_ASSERT_EQUAL(STATE_EMERGENCY_STOP,box.getCurrentState());
}


void test_warning_dwell_and_hysteresis_prevent_report_chatter(void) {
    BatteryPolicy p;uint32_t now=0;p.sample(now,12.2f,true,11.3f);
    now+=1000;p.sample(now,11.9f,true,11.3f);
    TEST_ASSERT_EQUAL(static_cast<int>(BatteryState::NORMAL),static_cast<int>(p.snapshot(now).state));
    for(int i=0;i<16;i++){now+=1000;p.sample(now,11.9f,true,11.3f);}
    TEST_ASSERT_EQUAL(static_cast<int>(BatteryState::WARNING),static_cast<int>(p.snapshot(now).state));
    for(int i=0;i<20;i++){now+=1000;p.sample(now,12.04f,true,11.3f);}
    TEST_ASSERT_EQUAL(static_cast<int>(BatteryState::WARNING),static_cast<int>(p.snapshot(now).state));
    for(int i=0;i<16;i++){now+=1000;p.sample(now,12.2f,true,11.3f);}
    TEST_ASSERT_EQUAL(static_cast<int>(BatteryState::NORMAL),static_cast<int>(p.snapshot(now).state));
}


void test_recovered_relay_direction_modes_and_next_close(void) {
    MockHardware hw;hw.setBatteryVoltage(12.6f);SmartBoxController box(hw);
    box.setInitialState(STATE_BATTERY_LOW_SHUTDOWN);box.init();
    for(int i=0;i<301;i++){hw.advanceMillis(1000);box.update();}
    hw.clearEventHistory();box.forceOpen();box.update();
    TEST_ASSERT_EQUAL(OUTPUT,hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(OUTPUT,hw.getPinMode(RELAY_DIR_B_PIN));
    TEST_ASSERT_EQUAL(LOW,hw.getPinValue(RELAY_DIR_A_PIN));
    hw.advanceMillis(4000);box.update();box.forceClose();box.update();
    TEST_ASSERT_EQUAL(OUTPUT,hw.getPinMode(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(OUTPUT,hw.getPinMode(RELAY_DIR_B_PIN));
    TEST_ASSERT_EQUAL(HIGH,hw.getPinValue(RELAY_DIR_A_PIN));
    TEST_ASSERT_EQUAL(LOW,hw.getPinValue(RELAY_DIR_B_PIN));
}
void test_close_command_cannot_leave_ota(void) {
    MockHardware hw;hw.setBatteryVoltage(12.6f);SmartBoxController box(hw);box.init();
    box.transitionTo(STATE_HOLD);box.transitionTo(STATE_OTA_UPDATING);
    box.forceClose();box.update();
    TEST_ASSERT_EQUAL(STATE_OTA_UPDATING,box.getCurrentState());
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
}
void test_reverse_current_stall_uses_magnitude(void) {
    MockHardware hw;SmartBoxController box(hw);box.init();box.forceOpen();box.update();
    hw.setMotorCurrent(-4000);hw.advanceMillis(600);
    box.update();box.update();box.update();
    TEST_ASSERT_EQUAL(STATE_HOLD,box.getCurrentState());
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
}


#ifdef NATIVE_BUILD
static void chargeForOta(MockHardware& hw,SmartBoxController& box) {
    hw.setBatteryVoltage(12.6f);
    for(int i=0;i<35;i++){hw.advanceMillis(1000);box.update();}
}
void test_atomic_ota_reservation_blocks_all_motion_commands(void) {
    MockHardware hw;hw.setBatteryVoltage(12.6f);SmartBoxController box(hw);box.init();chargeForOta(hw,box);
    TEST_ASSERT_TRUE(box.tryBeginOta());
    box.forceOpen();box.forceClose();box.startMaintenanceMode();box.stopMaintenanceMode();box.resetEmergency();box.update();
    TEST_ASSERT_EQUAL(STATE_OTA_UPDATING,box.getCurrentState());
    TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));TEST_ASSERT_TRUE(box.canContinueOta());
    hw.setBatteryVoltage(11);for(int i=0;i<17;i++){hw.advanceMillis(200);box.servicePowerDuringOta();}
    TEST_ASSERT_FALSE(box.canContinueOta());box.finishOtaWithoutReboot();
    TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN,box.getCurrentState());
}
class PausedContactHardware:public MockHardware {
 public:
    std::mutex gate;std::condition_variable cv;bool pause=false,entered=false,released=false;
    void delayMs(unsigned long ms) override {
        if(pause && ms==100){std::unique_lock<std::mutex> l(gate);entered=true;cv.notify_all();cv.wait(l,[this]{return released;});pause=false;}
        MockHardware::delayMs(ms);
    }
};
void test_ota_cannot_suspend_an_opening_interlock(void) {
    PausedContactHardware hw;hw.setBatteryVoltage(12.6f);SmartBoxController box(hw);box.init();chargeForOta(hw,box);box.forceOpen();hw.pause=true;
    std::thread loop([&]{box.update();});
    {std::unique_lock<std::mutex> l(hw.gate);hw.cv.wait(l,[&]{return hw.entered;});}
    auto reserve=std::async(std::launch::async,[&]{return box.tryBeginOta();});
    const bool blocked=reserve.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout;
    {std::lock_guard<std::mutex> l(hw.gate);hw.released=true;hw.cv.notify_all();}
    loop.join();const bool accepted=reserve.get();
    TEST_ASSERT_TRUE(blocked);TEST_ASSERT_FALSE(accepted);
    TEST_ASSERT_EQUAL(STATE_OPENING,box.getCurrentState());
    hw.advanceMillis(4000);box.update();TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
}
#endif


class RegisterInaBus:public Ina219Bus {
 public:
    uint32_t clock=0,trigger=0;uint16_t config=0,calibration=0;
    int16_t shunt=6,current=6;
    bool badCalWrite=false,badConfigWrite=false,badCal=false,badConfig=false,readFail=false,neverReady=false,overflow=false,oldOverflow=false;
    bool writeRegister(uint8_t reg,uint16_t v)override{
        if(reg==5){if(badCalWrite)return false;calibration=v;}
        if(reg==0){if(badConfigWrite)return false;config=v;if((v&7)==3)trigger=clock;}
        return true;
    }
    bool readRegister(uint8_t reg,uint16_t &v)override{
        if(readFail)return false;
        if(reg==0)v=badConfig?0:config;
        else if(reg==5)v=badCal?0:calibration;
        else if(reg==1)v=static_cast<uint16_t>(shunt);
        else if(reg==4)v=static_cast<uint16_t>(current);
        else if(reg==2){bool ready=!neverReady && uint32_t(clock-trigger)>=2;v=uint16_t((3158u<<3)|(ready?2:0)|((ready?overflow:oldOverflow)?1:0));}
        else return false;
        return true;
    }
    uint32_t nowMs()override{return clock;}
    void waitMs(uint32_t n)override{clock+=n;}
};
void test_ina_triggered_fresh_profile_and_units(void){
    RegisterInaBus bus;auto sample=sampleIna219(bus);
    TEST_ASSERT_TRUE(sample.valid);TEST_ASSERT_FLOAT_WITHIN(0.00002f,12.63206f,sample.voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f,0.6f,sample.currentMa);
    TEST_ASSERT_EQUAL_UINT16(0x3998,bus.config);TEST_ASSERT_EQUAL_UINT16(4096,bus.calibration);
}
void test_ina_bad_transfer_or_profile_is_not_valid(void){
    for(int i=0;i<5;i++){
        RegisterInaBus b;if(i==0)b.badCalWrite=true;if(i==1)b.badConfigWrite=true;if(i==2)b.badCal=true;if(i==3)b.badConfig=true;if(i==4)b.readFail=true;
        TEST_ASSERT_FALSE(sampleIna219(b).valid);
    }
}
void test_ina_stale_conversion_and_overflow_are_not_valid(void){
    RegisterInaBus stale;stale.neverReady=true;TEST_ASSERT_FALSE(sampleIna219(stale).valid);TEST_ASSERT_LESS_OR_EQUAL_UINT32(20,stale.clock);
    RegisterInaBus overflow;overflow.overflow=true;TEST_ASSERT_FALSE(sampleIna219(overflow).valid);
    RegisterInaBus old;old.oldOverflow=true;TEST_ASSERT_TRUE(sampleIna219(old).valid);
}
void test_ina_saturation_and_incoherent_current_are_not_valid(void){
    for(int n:{32000,-32000,32767,-32768}){RegisterInaBus b;b.shunt=n;b.current=n;TEST_ASSERT_FALSE(sampleIna219(b).valid);}
    RegisterInaBus wrong;wrong.shunt=100;wrong.current=-100;TEST_ASSERT_FALSE(sampleIna219(wrong).valid);
    RegisterInaBus reverse;reverse.shunt=-6;reverse.current=-6;auto value=sampleIna219(reverse);TEST_ASSERT_TRUE(value.valid);TEST_ASSERT_FLOAT_WITHIN(0.0001f,-0.6f,value.currentMa);
}
void test_ina_conversion_timeout_clock_wrap(void){
    RegisterInaBus b;b.clock=UINT32_MAX-1;TEST_ASSERT_TRUE(sampleIna219(b).valid);
}


void test_12v_profile_overvoltage_cannot_enable_motor(void){
    MockHardware hw;hw.setBatteryVoltage(18.0f);SmartBoxController box(hw);box.init();box.forceOpen();box.update();
    TEST_ASSERT_FALSE(box.powerSnapshot().valid);TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(RELAY_MAIN_PIN));
}


class LegacyContinuousSource:public Ina219ContinuousSource {
 public:
  uint16_t config=0x399f,cal=4096;float bus=12.124f,shunt=0.04f,current=0.4f;int fail=0,configReads=0;
  bool readConfiguration(uint16_t &v)override{++configReads;v=config;return fail!=1;}
  bool readCalibration(uint16_t &v)override{v=cal;return fail!=2;}
  bool readBusVoltage(float &v)override{v=bus;return fail!=3;}
  bool readShuntVoltageMv(float &v)override{v=shunt;return fail!=4;}
  bool readCurrentMa(float &v)override{v=current;return fail!=5;}
};
void test_ina_continuous_baseline_reproduces_field_units(void){
 LegacyContinuousSource port;auto r=sampleIna219Continuous(port);
 TEST_ASSERT_TRUE(r.valid);TEST_ASSERT_FLOAT_WITHIN(0.00002f,12.12404f,r.voltage);TEST_ASSERT_FLOAT_WITHIN(0.0001f,0.4f,r.currentMa);
 TEST_ASSERT_EQUAL(1,r.driver);TEST_ASSERT_EQUAL(PowerReadFault::NONE,r.fault);TEST_ASSERT_EQUAL_UINT16(0x399f,r.configuration);TEST_ASSERT_EQUAL_UINT16(4096,r.calibration);
}
void test_ina_continuous_failed_reads_never_fabricate_power(void){
 for(int i=1;i<=5;i++){LegacyContinuousSource p;p.fail=i;auto r=sampleIna219Continuous(p);TEST_ASSERT_FALSE(r.valid);TEST_ASSERT_TRUE(std::isnan(r.voltage));TEST_ASSERT_TRUE(std::isnan(r.currentMa));TEST_ASSERT_NOT_EQUAL(PowerReadFault::NONE,r.fault);}
}
void test_ina_continuous_profile_calibration_and_numeric_failures_are_diagnosed(void){
 LegacyContinuousSource p;p.config=0x399b;auto r=sampleIna219Continuous(p);TEST_ASSERT_FALSE(r.valid);TEST_ASSERT_EQUAL(PowerReadFault::PROFILE_MISMATCH,r.fault);
 p.config=0x399f;p.cal=0;r=sampleIna219Continuous(p);TEST_ASSERT_EQUAL(PowerReadFault::CALIBRATION_MISMATCH,r.fault);
 for(float bad:{NAN,INFINITY,-1.0f,27.0f}){LegacyContinuousSource q;q.bus=bad;TEST_ASSERT_FALSE(sampleIna219Continuous(q).valid);}
 LegacyContinuousSource q;q.current=NAN;TEST_ASSERT_FALSE(sampleIna219Continuous(q).valid);
}
void test_ina_continuous_bidirectional_read_does_not_require_triggered_cohort(void){
 LegacyContinuousSource p;p.current=-0.6f;p.shunt=-0.06f;auto r=sampleIna219Continuous(p);TEST_ASSERT_TRUE(r.valid);TEST_ASSERT_FLOAT_WITHIN(0.00002f,12.12394f,r.voltage);
 p.current=50.0f;r=sampleIna219Continuous(p);TEST_ASSERT_TRUE(r.valid); // Continuous registers may advance between independent reads.
}


void test_sensor_fault_recovery_ota_is_explicit_isolated_and_once_per_boot(void){
 MockHardware hw;hw.setBatteryVoltage(NAN);SmartBoxController box(hw);box.init();box.update();
 TEST_ASSERT_FALSE(box.canStartOta(true));hw.advanceMillis(120000);box.update();
 TEST_ASSERT_FALSE(box.canStartOta());TEST_ASSERT_TRUE(box.canStartOta(true));TEST_ASSERT_TRUE(box.tryBeginOta(true));
 TEST_ASSERT_EQUAL(STATE_OTA_UPDATING,box.getCurrentState());TEST_ASSERT_TRUE(box.canContinueOta());
 box.forceOpen();box.forceClose();box.resetEmergency();box.startMaintenanceMode();box.stopMaintenanceMode();
 TEST_ASSERT_EQUAL(STATE_OTA_UPDATING,box.getCurrentState());for(int pin:{6,7,8})TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(pin));
 box.finishOtaWithoutReboot();TEST_ASSERT_EQUAL(STATE_BATTERY_LOW_SHUTDOWN,box.getCurrentState());TEST_ASSERT_FALSE(box.tryBeginOta(true));
}
void test_recovery_ota_cannot_bypass_known_critical_or_moving_or_stale_state(void){
 MockHardware hw;hw.setBatteryVoltage(11.0f);SmartBoxController box(hw);box.init();for(int n=0;n<121;n++){hw.advanceMillis(1000);box.update();}TEST_ASSERT_FALSE(box.tryBeginOta(true));
 MockHardware invalid;invalid.setBatteryVoltage(NAN);SmartBoxController b(invalid);b.init();b.update();invalid.advanceMillis(120000);b.update();invalid.advanceMillis(2001);TEST_ASSERT_FALSE(b.tryBeginOta(true));
 MockHardware over;over.setBatteryVoltage(18.0f);SmartBoxController high(over);high.init();high.update();over.advanceMillis(120000);over.setBatteryVoltage(NAN);high.update();TEST_ASSERT_FALSE(high.tryBeginOta(true));
 MockHardware good;good.setBatteryVoltage(12.6f);SmartBoxController moving(good);moving.init();for(int n=0;n<121;n++){good.advanceMillis(1000);moving.update();}moving.forceOpen();moving.update();TEST_ASSERT_FALSE(moving.tryBeginOta(true));
}
void test_recovery_ota_rechecks_known_voltage_and_expires_before_flash(void){
 MockHardware hw;hw.setBatteryVoltage(NAN);SmartBoxController box(hw);box.init();box.update();hw.advanceMillis(120000);box.update();TEST_ASSERT_TRUE(box.tryBeginOta(true));
 hw.setBatteryVoltage(10.8f);hw.advanceMillis(1000);box.servicePowerDuringOta();TEST_ASSERT_FALSE(box.canContinueOta());box.finishOtaWithoutReboot();for(int pin:{6,7,8})TEST_ASSERT_EQUAL(INPUT,hw.getPinMode(pin));
 MockHardware t;t.setBatteryVoltage(NAN);SmartBoxController timed(t);timed.init();timed.update();t.advanceMillis(120000);timed.update();TEST_ASSERT_TRUE(timed.tryBeginOta(true));t.advanceMillis(120001);timed.servicePowerDuringOta();TEST_ASSERT_FALSE(timed.canContinueOta());
}

void test_recovery_request_requires_fresh_bounded_owner_shape(void){
 uint32_t ttl=99;const char* id="0123456789abcdef0123456789abcdef";
 TEST_ASSERT_TRUE(validateRecoveryOtaRequest(id,1800000120,1800000000,ttl));TEST_ASSERT_EQUAL_UINT32(120000,ttl);
 const uint64_t expired[]={1799999999ULL,1800000000ULL,1800000121ULL,UINT64_MAX};
 for(uint64_t expires:expired){TEST_ASSERT_FALSE(validateRecoveryOtaRequest(id,expires,1800000000,ttl));TEST_ASSERT_EQUAL_UINT32(0,ttl);}
 TEST_ASSERT_FALSE(validateRecoveryOtaRequest(nullptr,1800000010,1800000000,ttl));TEST_ASSERT_FALSE(validateRecoveryOtaRequest("bad",1800000010,1800000000,ttl));
 TEST_ASSERT_FALSE(validateRecoveryOtaRequest("0123456789abcdef0123456789abcdeg",1800000010,1800000000,ttl));TEST_ASSERT_FALSE(validateRecoveryOtaRequest(id,1000,1,ttl));
}

void test_recovery_cannot_forget_known_critical_voltage_after_read_failure(void){
 MockHardware hw;hw.setBatteryVoltage(11.0f);SmartBoxController box(hw);box.init();for(int n=0;n<121;n++){hw.advanceMillis(1000);box.update();}
 hw.setBatteryVoltage(NAN);hw.advanceMillis(1000);box.update();TEST_ASSERT_FALSE(box.canStartOta(true));TEST_ASSERT_FALSE(box.tryBeginOta(true));
 hw.setBatteryVoltage(12.6f);hw.advanceMillis(1000);box.update();hw.setBatteryVoltage(NAN);hw.advanceMillis(1000);box.update();TEST_ASSERT_TRUE(box.canStartOta(true));
}

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_relay_glitch_protection);
    RUN_TEST(test_relay_interlock_and_guard_delay);
    RUN_TEST(test_stall_current_detection);
    RUN_TEST(test_low_battery_shutdown);
    RUN_TEST(test_ultrasonic_median_filter);
    RUN_TEST(test_config_and_callback_trigger);
    RUN_TEST(test_hold_duration_and_retrigger);
    RUN_TEST(test_startup_open_flow);
    RUN_TEST(test_ota_state_isolation);
    RUN_TEST(test_maintenance_mode);
    RUN_TEST(test_post_close_blind_time);
    RUN_TEST(test_telemetry_interval_config);
    RUN_TEST(test_sensor_deadlock_prevention);
    RUN_TEST(test_battery_recovers_only_after_continuous_safe_voltage);
    RUN_TEST(test_battery_recovery_dip_restarts_stable_window);
    RUN_TEST(test_bad_voltage_never_operates_motor);
    RUN_TEST(test_sensor_failure_during_protective_stroke_stops_now);
    RUN_TEST(test_restored_protection_cannot_repeat_emergency_stroke);
    RUN_TEST(test_recovery_gap_cannot_count_as_continuous_charge);
    RUN_TEST(test_policy_recovers_across_u32_wrap);
    RUN_TEST(test_expired_measurement_denies_commands_and_ota);
    RUN_TEST(test_low_timers_do_not_cross_controller_instances);
    RUN_TEST(test_ota_failed_recovery_cannot_clear_low_power_latch);
    RUN_TEST(test_subthreshold_grace_does_not_start_normal_motor);
    RUN_TEST(test_power_fault_cannot_be_cleared_by_emergency_reset);
    RUN_TEST(test_warning_dwell_and_hysteresis_prevent_report_chatter);
    RUN_TEST(test_recovered_relay_direction_modes_and_next_close);
    RUN_TEST(test_close_command_cannot_leave_ota);
    RUN_TEST(test_reverse_current_stall_uses_magnitude);
    RUN_TEST(test_12v_profile_overvoltage_cannot_enable_motor);
    RUN_TEST(test_ina_triggered_fresh_profile_and_units);
    RUN_TEST(test_ina_bad_transfer_or_profile_is_not_valid);
    RUN_TEST(test_ina_stale_conversion_and_overflow_are_not_valid);
    RUN_TEST(test_ina_saturation_and_incoherent_current_are_not_valid);
    RUN_TEST(test_ina_conversion_timeout_clock_wrap);
    RUN_TEST(test_ina_continuous_baseline_reproduces_field_units);
    RUN_TEST(test_ina_continuous_failed_reads_never_fabricate_power);
    RUN_TEST(test_ina_continuous_profile_calibration_and_numeric_failures_are_diagnosed);
    RUN_TEST(test_ina_continuous_bidirectional_read_does_not_require_triggered_cohort);
    RUN_TEST(test_recovery_cannot_forget_known_critical_voltage_after_read_failure);
    RUN_TEST(test_recovery_request_requires_fresh_bounded_owner_shape);
    RUN_TEST(test_sensor_fault_recovery_ota_is_explicit_isolated_and_once_per_boot);
    RUN_TEST(test_recovery_ota_cannot_bypass_known_critical_or_moving_or_stale_state);
    RUN_TEST(test_recovery_ota_rechecks_known_voltage_and_expires_before_flash);
    RUN_TEST(test_atomic_ota_reservation_blocks_all_motion_commands);
    RUN_TEST(test_ota_cannot_suspend_an_opening_interlock);
    return UNITY_END();
}
#else
void setup() {
    // Wait for serial connection on the target board
    delay(2000);
    
    UNITY_BEGIN();
    RUN_TEST(test_relay_glitch_protection);
    RUN_TEST(test_relay_interlock_and_guard_delay);
    RUN_TEST(test_stall_current_detection);
    RUN_TEST(test_low_battery_shutdown);
    RUN_TEST(test_ultrasonic_median_filter);
    RUN_TEST(test_config_and_callback_trigger);
    RUN_TEST(test_hold_duration_and_retrigger);
    RUN_TEST(test_startup_open_flow);
    RUN_TEST(test_ota_state_isolation);
    RUN_TEST(test_maintenance_mode);
    RUN_TEST(test_post_close_blind_time);
    RUN_TEST(test_telemetry_interval_config);
    RUN_TEST(test_sensor_deadlock_prevention);
    RUN_TEST(test_battery_recovers_only_after_continuous_safe_voltage);
    RUN_TEST(test_battery_recovery_dip_restarts_stable_window);
    RUN_TEST(test_bad_voltage_never_operates_motor);
    RUN_TEST(test_sensor_failure_during_protective_stroke_stops_now);
    RUN_TEST(test_restored_protection_cannot_repeat_emergency_stroke);
    RUN_TEST(test_recovery_gap_cannot_count_as_continuous_charge);
    RUN_TEST(test_policy_recovers_across_u32_wrap);
    RUN_TEST(test_expired_measurement_denies_commands_and_ota);
    RUN_TEST(test_low_timers_do_not_cross_controller_instances);
    RUN_TEST(test_ota_failed_recovery_cannot_clear_low_power_latch);
    RUN_TEST(test_subthreshold_grace_does_not_start_normal_motor);
    RUN_TEST(test_power_fault_cannot_be_cleared_by_emergency_reset);
    RUN_TEST(test_warning_dwell_and_hysteresis_prevent_report_chatter);
    RUN_TEST(test_recovered_relay_direction_modes_and_next_close);
    RUN_TEST(test_close_command_cannot_leave_ota);
    RUN_TEST(test_reverse_current_stall_uses_magnitude);
    RUN_TEST(test_12v_profile_overvoltage_cannot_enable_motor);
    UNITY_END();
}

void loop() {
    // Keep target responsive but idle
}
#endif
