# Night Sleep Mode Implementation Plan

This plan describes how we will implement the **Night Sleep Mode (야간 절전 모드)** on the ESP32-C6 SmartBox. 

During nighttime (23:00 ~ 06:00 KST), the device will disable all radio communications (Wi-Fi) to conserve battery power, suspend non-essential background tasks (Telemetry and Auto-OTA), and yet continue to operate the core FSM and ultrasonic/motor peripherals for local lid operations.

---

## Proposed Changes

### 1. SmartBox Controller Integration

We need to track the active power mode in the core state machine and block telemetry while in night sleep mode.

#### [MODIFY] [SmartBoxController.h](../src/SmartBoxController.h)
- Add a private boolean field `nightSleepActive` initialized to `false`.
- Add public setter and getter methods:
  ```cpp
  bool isNightSleepActive() const { return nightSleepActive; }
  void setNightSleepMode(bool active) { nightSleepActive = active; }
  ```

#### [MODIFY] [SmartBoxController.cpp](../src/SmartBoxController.cpp)
- Initialize `nightSleepActive(false)` in the constructor initialization list.
- Modify `canSendTelemetry()` to return `false` if `nightSleepActive` is true.

---

### 2. Wi-Fi Manager Enhancements

We need explicit commands to cleanly shut down the ESP32 Wi-Fi radio and restore STA mode using saved credentials.

#### [MODIFY] [WifiManager.h](../src/WifiManager.h)
- Declare static methods:
  ```cpp
  static void stopWiFi();
  static void startWiFi(const char* ssid, const char* password);
  ```

#### [MODIFY] [WifiManager.cpp](../src/WifiManager.cpp)
- Implement `stopWiFi()`:
  - Disconnect from current AP.
  - Set `connected = false`.
  - Set mode to `WIFI_OFF`.
  - Print log `[WIFI] Wi-Fi module disabled (WIFI_OFF).`
- Implement `startWiFi(const char* ssid, const char* password)`:
  - Set mode to `WIFI_STA`.
  - Initiate connection using the credentials.
  - Print log `[WIFI] Wi-Fi STA mode enabled. Connecting to AP '%s'...`

---

### 3. Task Suspension (Telemetry & Auto-OTA)

We will bypass background operations when Night Sleep Mode is active to prevent redundant network checks and failures.

#### [MODIFY] [TelemetryManager.cpp](../src/TelemetryManager.cpp)
- In `TelemetryManager::update()`, add an early return check:
  ```cpp
  if (controllerPtr->isNightSleepActive()) {
      return;
  }
  ```

#### [MODIFY] [AutoOtaManager.cpp](../src/AutoOtaManager.cpp)
- In `AutoOtaManager::update()`, add an early return check:
  ```cpp
  if (controllerPtr->isNightSleepActive()) {
      return;
  }
  ```

---

### 4. Time-based Power Manager (New Component)

We will introduce a new manager to monitor the system time (from NTP) and control transitions between day and night modes.

#### [NEW] [PowerManager.h](../src/PowerManager.h)
- Declare static interface:
  ```cpp
  #ifndef POWER_MANAGER_H
  #define POWER_MANAGER_H

  #include "SmartBoxController.h"

  class PowerManager {
  private:
      static SmartBoxController* controllerPtr;
      static unsigned long lastCheckTime;

  public:
      static void init(SmartBoxController& controller);
      static void update();
  };

  #endif // POWER_MANAGER_H
  ```

#### [NEW] [PowerManager.cpp](../src/PowerManager.cpp)
- Implement time-based check:
  - Every 30 seconds (non-blocking), retrieve local system time via standard `getLocalTime(&timeinfo, 0)`.
  - Determine if current hour falls in the night sleep window (23:00 to 06:00): `timeinfo.tm_hour >= 23 || timeinfo.tm_hour < 6`.
  - **Enter Sleep:** If night time and `controllerPtr->isNightSleepActive()` is `false`:
    - Call `controllerPtr->setNightSleepMode(true);`
    - Call `WifiManager::stopWiFi();`
    - Log: `[POWER] 진입: 야간 절전 모드 활성화. Wi-Fi OFF.`
  - **Exit Sleep (Wake):** If day time and `controllerPtr->isNightSleepActive()` is `true`:
    - Call `controllerPtr->setNightSleepMode(false);`
    - Load saved credentials using `ConfigManager::loadWifiCredentials(ssid, pass)`.
    - Call `WifiManager::startWiFi(ssid.c_str(), pass.c_str());`
    - Log: `[POWER] 해제: 주간 모드 전환. Wi-Fi 재연결 시도 중...`

---

### 5. Integration in Main Application Loop

#### [MODIFY] [main.cpp](../src/main.cpp)
- Include `PowerManager.h`.
- Initialize in `setup()`: `PowerManager::init(controller);`
- Call in `loop()`: `PowerManager::update();` (immediately after `controller.update()`).

#### [MODIFY] [platformio.ini](../platformio.ini)
- Exclude `PowerManager.cpp` from `esp32-c6-test` and `native` environments by adding `-<PowerManager.cpp>` to `build_src_filter` to ensure target tests compile cleanly without network dependencies.

---

### 6. Testing

#### [MODIFY] [test_main.cpp](../test/test_native/test_main.cpp)
- Add a unit test `test_night_sleep_mode()` verifying:
  - Initial state is false.
  - Setting `setNightSleepMode(true)` works.
  - When night sleep is active, `canSendTelemetry()` returns false.

---

## Verification Plan

### Automated Tests
1. Run `esp32-c6-test` build:
   `pio run -e esp32-c6-test`
2. Validate that the new unit test compiles and passes correctly.

### Manual Verification
1. Flash firmware to target board.
2. Verify startup logs, Wi-Fi initialization, and NTP synchronization.
3. Simulate/mock night sleep time transition (by changing the range check or system clock temporarily) and verify serial prints:
   - `[POWER] 진입: 야간 절전 모드 활성화. Wi-Fi OFF.`
   - `[WIFI] Wi-Fi module disabled (WIFI_OFF).`
4. Verify that local proximity triggering (motion sensor opening/closing the lid) still works normally during sleep.
5. Simulate day time wake transition and verify prints:
   - `[POWER] 해제: 주간 모드 전환. Wi-Fi 재연결 시도 중...`
   - `[WIFI] Wi-Fi STA mode enabled. Connecting to AP...`
