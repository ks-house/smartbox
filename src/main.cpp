#include <Arduino.h>
#include "Esp32Hardware.h"
#include "SmartBoxController.h"
#include "ConfigManager.h"
#include "WifiManager.h"

#include "WebDashboard.h"
#include "AutoOtaManager.h"
#include "TelemetryManager.h"
#include "PowerManager.h"
#include "RemoteLogger.h"

#ifndef NATIVE_BUILD
#include <esp_task_wdt.h>
#endif

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
    

    // Save logical lid state in Preferences based on transition
    if (newState == STATE_HOLD || newState == STATE_OPENING || newState == STATE_BATTERY_LOW_SHUTDOWN || newState == STATE_STARTUP_OPEN) {
        ConfigManager::saveLidState(true);
    } else if (newState == STATE_IDLE || newState == STATE_CLOSING) {
        ConfigManager::saveLidState(false);
    }
}

#ifndef NATIVE_BUILD
static void NetworkTask(void* pvParameters) {
    Serial.println("[SYSTEM] Background NetworkTask loop started.");
    
    // Register background task to TWDT
    esp_task_wdt_add(NULL);
    
    for (;;) {
        // Feed watchdog
        esp_task_wdt_reset();
        
        // Run Auto-OTA scheduler
        AutoOtaManager::update();
        
        // Run InfluxDB Telemetry updates
        TelemetryManager::update();
        
        // Yield CPU
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

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
    WifiManager::init("SmartBox-WiFi", SECRET_AP_PASS, savedSsid.c_str(), savedPass.c_str());

    // 6. Initialize Web Dashboard
    WebDashboard::init(controller);

    // 8. Initialize Auto-OTA scheduler and manager
    AutoOtaManager::init(controller);

    // 9. Initialize InfluxDB telemetry manager
    TelemetryManager::init(controller);

    // Register TelemetryManager log queue as WARN/ERROR forwarding target
    RemoteLogger::onWarnError = TelemetryManager::pushLog;
    Serial.println("[SYSTEM] RemoteLogger WARN/ERROR -> TelemetryManager::pushLog callback registered.");

    // 10. Initialize time-based power management
    PowerManager::init(controller);

#ifndef NATIVE_BUILD
    // Spawn network task with 16KB stack size for TLS handshake/InfluxDB/HTTPUpdate
    xTaskCreate(
        NetworkTask,
        "NetworkTask",
        16384,
        NULL,
        1, // Low priority background task
        NULL
    );
    Serial.println("[SYSTEM] Background NetworkTask created successfully.");
#endif

#ifndef NATIVE_BUILD
    // 11. Configure and initialize the Task Watchdog Timer (TWDT)
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    #else
    esp_task_wdt_init(10, true);
    #endif
    esp_task_wdt_add(NULL); // Subscribe loopTask to TWDT
    Serial.println("[SYSTEM] Task Watchdog Timer (TWDT) initialized (10s timeout, loopTask subscribed).");
#endif

    Serial.println("[SYSTEM] Initialization finished. FSM Loop running.");
}

void loop() {
#ifndef NATIVE_BUILD
    static bool loopTaskSubscribed = true;
    if (controller.isOtaMode()) {
        if (loopTaskSubscribed) {
            esp_task_wdt_delete(NULL);
            loopTaskSubscribed = false;
            Serial.println("[SYSTEM] loopTask unsubscribed from WDT for OTA");
        }
        delay(100); // Yield CPU to async web server for OTA data processing
        return;
    } else {
        if (!loopTaskSubscribed) {
            esp_task_wdt_add(NULL);
            loopTaskSubscribed = true;
            Serial.println("[SYSTEM] loopTask re-subscribed to WDT after OTA idle recovery");
        }
    }
    
    // Feed watchdog
    esp_task_wdt_reset();
#else
    // OTA 모드일 때 FSM/센서 루프 완전 정지 (이중 안전장치)
    if (controller.isOtaMode()) {
        delay(100); // Yield CPU to async web server for OTA data processing
        return;
    }
#endif

    // Run core FSM updates
    controller.update();

    // Run Wi-Fi manager updates
    WifiManager::update();

    // Run Web Dashboard updates
    WebDashboard::update();

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