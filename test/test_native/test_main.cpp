#ifndef NATIVE_BUILD
#include <Arduino.h>
#endif
#include <unity.h>
#include "MockHardware.h"

#include "SmartBoxController.h"

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
    
    // Run update 5 times with 50ms intervals to populate the median filter buffer
    for (int i = 0; i < 5; ++i) {
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
    
    // Trigger opening transition
    hw.setDistanceCm(30.0f);
    for (int i = 0; i < 5; ++i) {
        hw.advanceMillis(50);
        controller.update();
    }
    
    TEST_ASSERT_EQUAL(STATE_OPENING, controller.getCurrentState());
    
    // Advance virtual clock by 400ms (within 500ms inrush window)
    hw.advanceMillis(400);
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
    
    // Sample 3: exceeding threshold -> transition to EMERGENCY_STOP
    hw.setMotorCurrent(4000.0f);
    controller.update();
    TEST_ASSERT_EQUAL(STATE_EMERGENCY_STOP, controller.getCurrentState());
    
    // Relays must be isolated to INPUT (high impedance)
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
    // FSM must transition to STATE_OPEN_START
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
    for (int i = 0; i < 5; ++i) {
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
    for (int i = 0; i < 5; ++i) {
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
    for (int i = 0; i < 5; ++i) {
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
    UNITY_END();
}

void loop() {
    // Keep target responsive but idle
}
#endif
