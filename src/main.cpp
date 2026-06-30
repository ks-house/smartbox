#include <Arduino.h>
#include "Esp32Hardware.h"
#include "SmartBoxController.h"
#include "ConfigManager.h"
#include "WifiManager.h"

#include "AutoOtaManager.h"
#include "TelemetryManager.h"
#include "PowerManager.h"
#include "MqttManager.h"
#include "secrets.h"

#ifndef NATIVE_BUILD
#include <esp_task_wdt.h>
#endif

// Hardware pin assignments as per AGENTS.md
// TRIG = GPIO 4, ECHO = GPIO 5
static Esp32Hardware hardware(4, 5);
static SmartBoxController controller(hardware);
static MqttManager mqttManager(controller);

// NOTE: 이 함수는 사용 안 함. 모든 상태 변경 콜백은 setup()의 단일 통합 람다로 처리됩니다.
// (이전 버전과의 주석 호환성 유지용)

#ifndef NATIVE_BUILD
static void NetworkTask(void* pvParameters) {
    Serial.println("[SYSTEM] Background NetworkTask loop started.");
    
    // Register background task to TWDT
    esp_task_wdt_add(NULL);
    
    static unsigned long lastMqttTelemetry = 0;

    for (;;) {
        // Feed watchdog
        esp_task_wdt_reset();
        
        // Run Auto-OTA scheduler
        AutoOtaManager::update();
        
        // Run InfluxDB Telemetry updates
        TelemetryManager::update();

        // Run MQTT updates
        mqttManager.update();

        // Periodic MQTT Telemetry (every 30 seconds)
        if (millis() - lastMqttTelemetry >= 30000) {
            lastMqttTelemetry = millis();
            mqttManager.publishTelemetry();
        }
        
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

    // 3. Register unified state change callback (BUG-F fix: single lambda merges NVS + MQTT callbacks)
    //    이전에 onStateChanged()와 MQTT 람다를 분리 등록하면 두 번째 등록이 첫 번째를 덮어썼음.
    //    NVS 저장 + MQTT 이벤트를 하나의 람다로 통합하여 안전하게 처리합니다.
    static unsigned long cycleStartTime = 0;
    static float cyclePeakCurrent = 0.0f;
    static float cycleStartBatt = 0.0f;

    controller.registerStateCallback([](State prev, State next) {
        // --- Part 1: NVS 뚜껑 상태 영구 저장 (구 onStateChanged 기능) ---
        // OTA 모드 진입 시 플래시 쓰기 방지
        if (next == STATE_OTA_UPDATING) {
            Serial.println("[SYSTEM] OTA mode entered. All background routines suspended.");
        } else {
            // 논리적 뚜껑 상태를 Preferences에 저장
            if (next == STATE_HOLD || next == STATE_OPENING ||
                next == STATE_BATTERY_LOW_SHUTDOWN || next == STATE_STARTUP_OPEN) {
                ConfigManager::saveLidState(true);
            } else if (next == STATE_IDLE || next == STATE_CLOSING) {
                ConfigManager::saveLidState(false);
            }
        }

        // --- Part 2: MQTT 실시간 이벤트 발행 ---
        MqttManager* mqtt = getMqttManagerInstance();
        if (mqtt) {
            mqtt->publishEventState((int)prev, (int)next);

            // 개폐 사이클 시작: IDLE → non-IDLE 전환
            if (prev == STATE_IDLE && next != STATE_IDLE) {
                cycleStartTime = millis();
                cyclePeakCurrent = 0.0f;
                cycleStartBatt = controller.getBatteryVoltage();
            }
            // NOTE-01 fix: 구동 중 피크 전류 샘플링
            if (next == STATE_OPENING || next == STATE_CLOSING) {
                float cur = controller.getMotorCurrent();
                if (cur > cyclePeakCurrent) cyclePeakCurrent = cur;
            }
            // 개폐 사이클 완료: non-IDLE → IDLE 전환 시 요약 발행
            if (next == STATE_IDLE && prev != STATE_IDLE && cycleStartTime > 0) {
                unsigned long duration = millis() - cycleStartTime;
                float endBatt = controller.getBatteryVoltage();
                mqtt->publishEventCycle(duration, cyclePeakCurrent, cycleStartBatt, endBatt);
                cycleStartTime = 0;
            }
            // 알람 이벤트 발행
            if (next == STATE_EMERGENCY_STOP) {
                mqtt->publishEventAlarm("STALL_OVERCURRENT", controller.getMotorCurrent(), "Motor stall / emergency stop triggered");
            } else if (next == STATE_BATTERY_LOW_SHUTDOWN) {
                mqtt->publishEventAlarm("LOW_BATTERY", controller.getBatteryVoltage(), "Critical low battery voltage detected");
            }
        }
    });

    // 4. Initialize core state machine
    controller.init();

    // 5. Initialize Wi-Fi (AP SSID: SmartBox-WiFi)
    String savedSsid = "";
    String savedPass = "";
    ConfigManager::loadWifiCredentials(savedSsid, savedPass);
    WifiManager::init("SmartBox-WiFi", SECRET_AP_PASS, savedSsid.c_str(), savedPass.c_str());

    // 8. Initialize Auto-OTA scheduler and manager
    AutoOtaManager::init(controller);

    // 9. Initialize MQTT Telemetry Manager
    TelemetryManager::init(controller);

    // 10. Initialize MQTT Manager
    mqttManager.begin();
    Serial.println("[SYSTEM] RemoteLogger WARN/ERROR -> MqttManager callback registered.");

    // 11. Initialize time-based power management
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

    // Motion detection edge check (<= 100cm)
    static float lastDist = 999.0f;
    float currentDist = controller.getDistance();
    if (lastDist > 100.0f && currentDist <= 100.0f) {
        MqttManager* mqtt = getMqttManagerInstance();
        if (mqtt) {
            mqtt->publishEventMotion(currentDist);
        }
    }
    lastDist = currentDist;

    // Run Wi-Fi manager updates
    WifiManager::update();

    // Run Power Manager updates
    PowerManager::update();

#ifdef NATIVE_BUILD
    mqttManager.update();
#endif

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