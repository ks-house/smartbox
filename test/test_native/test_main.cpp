#ifndef NATIVE_BUILD
#include <Arduino.h>
#endif
#include <unity.h>
#include "MockHardware.h"

#include "SmartBoxController.h"

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
    
    // Sample 3: exceeding threshold -> MUST transition to EMERGENCY_STOP (anti-pinch protection active)
    hw.setMotorCurrent(4000.0f);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_EMERGENCY_STOP, controller.getCurrentState());
    
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
    
    // Simulate no human approach (80cm)
    hw.setDistanceCm(80.0f);
    for (int i = 0; i < 5; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    
    // State should still be STARTUP_OPEN
    TEST_ASSERT_EQUAL(STATE_STARTUP_OPEN, controller.getCurrentState());
    
    // Simulate human approach (30cm)
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 5; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    
    // State should transition to STATE_HOLD
    TEST_ASSERT_EQUAL(STATE_HOLD, controller.getCurrentState());
    
    // Relays should still be off/isolated in STATE_HOLD
    TEST_ASSERT_EQUAL(INPUT, hw.getPinMode(RELAY_MAIN_PIN));
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
    //    during which stateTimer is reset. Use 205 ticks to guarantee >= 10000ms
    //    since the last timer reset (205 * 50ms = 10250ms > 10000ms + 100ms margin).
    hw.setDistanceCm(80.0f);
    for (int i = 0; i < 205; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    TEST_ASSERT_EQUAL(STATE_CLOSE_START, controller.getCurrentState());

    // 4. Complete closing -> STATE_IDLE (postCloseBlindStartTime set here)
    //    Keep sensor at 80cm during CLOSING to avoid triggering Safety Reopen logic.
    //    Set to 30cm AFTER STATE_IDLE is entered to simulate lid bounce.
    controller.update(); // CLOSE_START -> CLOSING
    // hw.setDistanceCm stays at 80.0f (safe: above threshold, no safety reopen triggered)
    hw.advanceMillis(3800);  // actuatorTime elapsed
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
    UNITY_END();
}

void loop() {
    // Keep target responsive but idle
}
#endif
