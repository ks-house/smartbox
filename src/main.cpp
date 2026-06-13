#include <Arduino.h>
#include "Esp32Hardware.h"
#include "SmartBoxController.h"
#include "ConfigManager.h"
#include "WifiManager.h"
#include "SmartThingsReporter.h"
#include "WebDashboard.h"

// Hardware pin assignments as per AGENTS.md
// TRIG = GPIO 4, ECHO = GPIO 5
static Esp32Hardware hardware(4, 5);
static SmartBoxController controller(hardware);

// Callback to dispatch state changes asynchronously
static void onStateChanged(State prevState, State newState) {
    SmartThingsReporter::reportStateChange(prevState, newState);
    
    // Save logical lid state in Preferences based on transition
    if (newState == STATE_HOLD || newState == STATE_OPENING || newState == STATE_BATTERY_LOW_SHUTDOWN || newState == STATE_STARTUP_OPEN) {
        ConfigManager::saveLidState(true);
    } else if (newState == STATE_IDLE || newState == STATE_CLOSING) {
        ConfigManager::saveLidState(false);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500); // Small delay for serial stability
    Serial.println("[SYSTEM] Starting SmartBox Controller...");

    // 1. Initialize hardware binding
    hardware.init();

    // 2. Load configuration from Preferences
    BoxConfig config;
    ConfigManager::loadConfig(config);
    controller.setConfig(config);

    // Load initial lid state and set FSM start state
    bool lidWasOpen = ConfigManager::loadLidState();
    if (lidWasOpen) {
        controller.setInitialState(STATE_STARTUP_OPEN);
    }

    // 3. Register state change callback
    controller.registerStateCallback(onStateChanged);

    // 4. Initialize core state machine
    controller.init();

    // 5. Initialize Wi-Fi (AP SSID: SmartBox-WiFi)
    WifiManager::init("SmartBox-WiFi", "");

    // 6. Initialize Web Dashboard
    WebDashboard::init(controller);

    // 7. Initialize SmartThings webhook reporter
    SmartThingsReporter::init(nullptr); // Webhook can be passed here

    Serial.println("[SYSTEM] Initialization finished. FSM Loop running.");
}

void loop() {
    // Run core FSM updates
    controller.update();

    // Run Wi-Fi manager updates
    WifiManager::update();

    // Run Web Dashboard updates
    WebDashboard::update();

    // 1-second interval diagnostic print
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug >= 1000) {
        lastDebug = millis();
        Serial.printf("[DEBUG] Dist: %.1f cm, Batt: %.2f V, Curr: %.1f mA, State: %d\n",
                      controller.getDistance(),
                      controller.getBatteryVoltage(),
                      controller.getMotorCurrent(),
                      (int)controller.getCurrentState());
    }
}