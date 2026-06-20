#include <Arduino.h>
#include "Esp32Hardware.h"
#include "SmartBoxController.h"
#include "ConfigManager.h"
#include "WifiManager.h"
#include "SmartThingsReporter.h"
#include "WebDashboard.h"
#include "AutoOtaManager.h"
#include "TelemetryManager.h"
#include "PowerManager.h"

// Hardware pin assignments as per AGENTS.md
// TRIG = GPIO 4, ECHO = GPIO 5
static Esp32Hardware hardware(4, 5);
static SmartBoxController controller(hardware);

// Callback to dispatch state changes asynchronously
static void onStateChanged(State prevState, State newState) {
    // OTA 모드 진입 시 lid state 변경하지 않음 (OTA 중 플래시 쓰기 방지)
    if (newState == STATE_OTA_UPDATING) {
        Serial.println("[SYSTEM] OTA mode entered. All background routines suspended.");
        return;
    }
    
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
    String savedSsid = "";
    String savedPass = "";
    ConfigManager::loadWifiCredentials(savedSsid, savedPass);
    WifiManager::init("SmartBox-WiFi", "", savedSsid.c_str(), savedPass.c_str());

    // 6. Initialize Web Dashboard
    WebDashboard::init(controller);

    // 7. Initialize SmartThings webhook reporter
    SmartThingsReporter::init(nullptr); // Webhook can be passed here

    // 8. Initialize Auto-OTA scheduler and manager
    AutoOtaManager::init(controller);

    // 9. Initialize InfluxDB telemetry manager
    TelemetryManager::init(controller);

    // 10. Initialize time-based power management
    PowerManager::init(controller);

    Serial.println("[SYSTEM] Initialization finished. FSM Loop running.");
}

void loop() {
    // OTA 모드일 때 FSM/센서 루프 완전 정지 (이중 안전장치)
    if (controller.isOtaMode()) {
        delay(100); // Yield CPU to async web server for OTA data processing
        return;
    }

    // Run core FSM updates
    controller.update();

    // Run Wi-Fi manager updates
    WifiManager::update();

    // Run Web Dashboard updates
    WebDashboard::update();

    // Run Auto-OTA scheduler
    AutoOtaManager::update();

    // Run InfluxDB Telemetry updates
    TelemetryManager::update();

    // Run Power Manager updates
    PowerManager::update();

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