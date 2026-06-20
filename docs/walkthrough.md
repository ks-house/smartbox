# SmartBox Long-term Survivability Optimization Walkthrough

This document outlines the modifications made to optimize memory usage, establish FreeRTOS Task Watchdog Timer (TWDT) monitoring, and schedule a daily proactive reboot at 04:00 KST to prevent heap fragmentation.

---

## 1. Heap Memory Optimization (String Reservation)

To eliminate frequent dynamic allocations that cause heap fragmentation:
- **[WebDashboard.cpp](../src/WebDashboard.cpp):** Added `json.reserve(512)` in the `/api/status` endpoint to allocate the memory buffer for the FSM telemetry JSON structure in a single operation. In the `/api/wifi/scan` endpoint, reserved `results.length() + 64` bytes for the complete network results wrap.
- **[WifiManager.cpp](../src/WifiManager.cpp):** Enhanced `getScanResultsJson()` to reserve `32 + n * 64` bytes beforehand, avoiding multiple internal string reallocations inside the scan results rendering loop.

---

## 2. Task Watchdog Timer (TWDT) Integration

To ensure automatic recovery if any thread hangs:
- **[main.cpp](../src/main.cpp):** Included `<esp_task_wdt.h>` and initialized the task watchdog in `setup()` with a **10-second (10,000ms)** timeout and panic reboot enabled. 
- Subscribed both `loopTask` (in `setup()`) and the background `NetworkTask` (in `NetworkTask()`) using `esp_task_wdt_add(NULL)` and regularly feed the watchdog with `esp_task_wdt_reset()`.
- **OTA Exception Handling:** To prevent watchdog resets during the blocking flash write phase of an OTA firmware update:
  - In `loop()`, when entering `STATE_OTA_UPDATING`, the `loopTask` deregisters from the watchdog using `esp_task_wdt_delete(NULL)`. If recovery occurs, it re-subscribes.
  - In **[AutoOtaManager.cpp](../src/AutoOtaManager.cpp)**, right before flashing starts, the `NetworkTask` deregisters itself using `esp_task_wdt_delete(NULL)`. If flashing fails or has no updates, it re-subscribes.

---

## 3. Daily Proactive Reboot

To completely refresh system resources periodically:
- **[PowerManager.cpp](../src/PowerManager.cpp):** Checked local time in KST during the 15-second update loop. When the hour reaches `4` (04:00 AM), the state is `STATE_IDLE`, and the motor is fully stopped, the system restarts using `ESP.restart()`.
- Used the `Preferences` library (namespace `"smartbox"`, key `"reboot_day"`) to persist the day of the year (`timeinfo.tm_yday`). This ensures that the reboot executes exactly once, preventing infinite reboot loops within the 4:00-4:59 AM window.

---

## 4. Deep Power Diet — Dynamic CPU Frequency Scaling & Adaptive Sensor Polling

To minimize power consumption during night sleep mode (23:00–06:00):

### 4.1 Dynamic CPU Frequency Scaling
- **[PowerManager.cpp](../src/PowerManager.cpp):** Added `setCpuFrequencyMhz()` calls to scale the CPU clock dynamically based on the power mode:
  - **Night sleep entry** (Wi-Fi OFF): CPU scaled down to **80 MHz** — the minimum clock that can still execute core logic (sensor polling, state machine, relay control).
  - **Daytime resume** (Wi-Fi ON): CPU restored to **160 MHz** before Wi-Fi reconnection.
  - Both calls are wrapped in `#ifndef NATIVE_BUILD` to prevent compilation errors in the native test environment.
  - Serial log output: `[POWER] CPU Frequency scaled to XX MHz`

### 4.2 Adaptive Sensor Polling Rate
- **[SmartBoxController.cpp](../src/SmartBoxController.cpp):** Modified the ultrasonic sensor polling logic in `update()`:
  - **Daytime**: Polls every **50ms** (unchanged).
  - **Night sleep**: Polls every **250ms** (5× reduction in sensor reads).
  - Implementation uses a ternary operator: `unsigned long pollInterval = isSleep ? 250 : 50;`
  - Previous behavior completely suppressed sensor polling during night mode. The new design keeps basic lid functionality active at night while significantly reducing CPU wake cycles.

### 4.3 Test Update
- **[test_main.cpp](../test/test_native/test_main.cpp):** Updated `test_night_sleep_mode` to validate the adaptive polling behavior:
  - Verifies that sensor data does NOT update within the first 200ms (below the 250ms night polling interval).
  - Verifies that sensor data DOES converge after sufficient 250ms polling cycles, confirming the median filter operates correctly at the slower rate.
  - Telemetry suppression during night mode is still validated.

---

## 5. Verification

- **ESP32-C6 firmware build:** CI pipeline will verify compilation via `pio run -e esp32-c6-devkitc-1`.
- **Native test suite:** CI pipeline will verify `pio test -e native` passes with the updated adaptive polling test.
