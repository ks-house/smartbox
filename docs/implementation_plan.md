# Implementation Plan - SmartBox Memory Optimization & Watchdog Integration

This plan implements heap memory optimization to prevent fragmentation, registers a FreeRTOS Task Watchdog Timer (TWDT) for system reliability, and configures a daily proactive reboot.

## User Review Required

> [!IMPORTANT]
> - The Task Watchdog Timer (TWDT) will reboot the ESP32-C6 if either the main `loop()` or the background `NetworkTask` freezes for more than 10 seconds.
> - To prevent reboot loops during OTA flashing, both tasks will temporarily deregister from the TWDT when entering the OTA state, and will re-register if the OTA update fails.
> - A daily proactive reboot is configured at 4:00 AM KST when the system is in `STATE_IDLE` and the motor is fully stopped, resetting memory fragmentation.

---

## Proposed Changes

### 1. Heap Memory Optimization (String Reservation)

To prevent heap fragmentation due to frequent dynamic `String` reallocations in the web server response and network scan output:

#### [MODIFY] [WebDashboard.cpp](../src/WebDashboard.cpp)
- Call `json.reserve(512);` at the start of `/api/status` response generation.
- Call `json.reserve(results.length() + 64);` inside the `/api/wifi/scan` complete state response generation.

#### [MODIFY] [WifiManager.cpp](../src/WifiManager.cpp)
- Optimize `WifiManager::getScanResultsJson()` by calling `json.reserve(32 + n * 64);` to allocate the required buffer once, rather than reallocating dynamically on each loop iteration.

---

### 2. Task Watchdog Timer (TWDT) Integration

#### [MODIFY] [main.cpp](../src/main.cpp)
- Include `<esp_task_wdt.h>` under `#ifndef NATIVE_BUILD`.
- In `setup()` (under `#ifndef NATIVE_BUILD`), initialize TWDT using version-safe configuration for ESP32 Arduino Core 3.x (checking `ESP_ARDUINO_VERSION_MAJOR >= 3`):
  - Timeout: 10,000ms.
  - Enable panic reboot.
  - Subscribe the `loopTask` (using `esp_task_wdt_add(NULL)`).
- In `NetworkTask` loop, subscribe the task with `esp_task_wdt_add(NULL)` and call `esp_task_wdt_reset()` on each loop iteration.
- In `loop()`, call `esp_task_wdt_reset()` at the top.
- Add an OTA guard to the main `loop()`: if the controller is in OTA mode (`STATE_OTA_UPDATING`), unsubscribe the `loopTask` from the TWDT to prevent resets during flashing. Re-subscribe if it returns to `STATE_IDLE` (recovery from OTA failure).

#### [MODIFY] [AutoOtaManager.cpp](../src/AutoOtaManager.cpp)
- Include `<esp_task_wdt.h>` under `#ifndef NATIVE_BUILD`.
- Inside `runOtaProcess(bool force)`, unsubscribe the calling `NetworkTask` from the WDT before starting the blocking OTA flash download: `esp_task_wdt_delete(NULL)`.
- If the OTA update fails (e.g., `HTTP_UPDATE_FAILED` or `HTTP_UPDATE_NO_UPDATES`), re-subscribe `NetworkTask` to the WDT: `esp_task_wdt_add(NULL)`.

---

### 3. Proactive Daily Reboot Scheduling

#### [MODIFY] [PowerManager.cpp](../src/PowerManager.cpp)
- In `PowerManager::update()`, check if `timeinfo.tm_hour == 4` (04:00 AM KST), state is `STATE_IDLE`, and `!controllerPtr->isMotorRunning()`.
- Use `Preferences` (under namespace `"smartbox"`, key `"reboot_day"`) to read and write the day of the year (`timeinfo.tm_yday`).
- If `reboot_day` does not match the current day, save the current day to `Preferences` and call `ESP.restart()`. This guarantees the reboot only triggers once per day.

---

## Verification Plan

### Automated Tests
- Run PlatformIO native unit tests to ensure that the code compiles and existing tests pass:
  ```powershell
  pio test -e native
  ```

### Manual Verification
- Verify compilation of the firmware:
  ```powershell
  pio run -e esp32-c6-devkitc-1
  ```
- Upload and monitor the serial logs to verify successful WDT initialization, feeding logs, and verify that the device is running stably.
- Trigger manual OTA through the dashboard to verify that WDT doesn't reboot the device mid-OTA.
