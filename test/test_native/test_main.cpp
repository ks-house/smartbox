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

void setup() {
    // Wait for serial connection on the target board
    delay(2000);
    
    UNITY_BEGIN();
    RUN_TEST(test_relay_glitch_protection);
    UNITY_END();
}

void loop() {
    // Keep target responsive but idle
}

