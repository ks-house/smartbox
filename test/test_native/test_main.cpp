#include <Arduino.h>
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


void setup() {
    // Wait for serial connection on the target board
    delay(2000);
    
    UNITY_BEGIN();
    RUN_TEST(test_relay_glitch_protection);
    RUN_TEST(test_relay_interlock_and_guard_delay);
    UNITY_END();
}


void loop() {
    // Keep target responsive but idle
}

