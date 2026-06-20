# Walkthrough - Night Sleep Mode Implementation

We have implemented the **Night Sleep Mode (야간 절전 모드)** on the ESP32-C6 SmartBox. 

During nighttime (23:00 ~ 06:00 KST), this feature disables the Wi-Fi radio to conserve battery power, bypasses periodic InfluxDB telemetry reports and HTTPS OTA check tasks, while keeping the local finite state machine and ultrasonic sensor loop fully active for touchless lid operation.

---

## Changes Made

### 1. SmartBox Controller (`SmartBoxController`)
- **[SmartBoxController.h](../src/SmartBoxController.h)**:
  - Added the `nightSleepActive` boolean flag.
  - Added getter `isNightSleepActive()` and setter `setNightSleepMode()`.
- **[SmartBoxController.cpp](../src/SmartBoxController.cpp)**:
  - Initialized `nightSleepActive` to `false` in the constructor.
  - Updated `canSendTelemetry()` to return `false` if `nightSleepActive` is true.

### 2. Wi-Fi Manager (`WifiManager`)
- **[WifiManager.h](../src/WifiManager.h)** & **[WifiManager.cpp](../src/WifiManager.cpp)**:
  - Implemented `stopWiFi()`: Shuts down the station/AP connection cleanly and sets mode to `WIFI_OFF`.
  - Implemented `startWiFi(ssid, pass)`: Sets mode to `WIFI_STA` and connects to the specified SSID.

### 3. Background Tasks Suspension
- **[TelemetryManager.cpp](../src/TelemetryManager.cpp)**:
  - Early exit in `update()` if `isNightSleepActive()` is true, skipping any telemetry activity.
- **[AutoOtaManager.cpp](../src/AutoOtaManager.cpp)**:
  - Early exit in `update()` if `isNightSleepActive()` is true, skipping scheduled update checks.

### 4. Power Manager (`PowerManager`) - **[NEW]**
- **[PowerManager.h](../src/PowerManager.h)** & **[PowerManager.cpp](../src/PowerManager.cpp)**:
  - Checks the local system time (synced via NTP) every 15 seconds.
  - Switches to night sleep mode if current KST hour is `23` or between `0` and `5` (`currentHour >= 23 || currentHour < 6`), turning off Wi-Fi and logging `[POWER] 진입: 야간 절전 모드 활성화. Wi-Fi OFF.`.
  - Switches back to day mode if current KST hour is between `6` and `22`, turning on Wi-Fi, reloading saved credentials, initiating reconnect, and logging `[POWER] 해제: 주간 모드 전환. Wi-Fi 재연결 시도 중...`.

### 5. Main Integration
- **[main.cpp](../src/main.cpp)**:
  - Included `PowerManager.h`.
  - Initialized `PowerManager::init(controller)` in `setup()`.
  - Added `PowerManager::update()` in the main `loop()`.

### 6. Build & Test Settings
- **[platformio.ini](../platformio.ini)**:
  - Excluded `PowerManager.cpp` from tests/native builds.
- **[test_main.cpp](../test/test_native/test_main.cpp)**:
  - Added `test_night_sleep_mode()` unit test verifying initial state, flag toggle, telemetry blocking, and normal FSM operation while sleep mode is active.

---

## Verification Results

### Target Binary Build Compilation
- Environment `esp32-c6-devkitc-1` compiled and linked successfully:
  - **RAM:** `14.0%` (used 45792 bytes of 327680 bytes)
  - **Flash:** `19.7%` (used 1289988 bytes of 6553600 bytes)
  - Successfully produced combined firmware binary.

### Target Test Build Compilation
- Environment `esp32-c6-test` compiled and linked the tests successfully, confirming unit tests compile properly on RISC-V GCC target compiler.
