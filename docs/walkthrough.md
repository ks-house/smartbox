# Walkthrough - Auto-OTA System Implementation

The automatic Over-The-Air (Auto-OTA) system for the ESP32-C6 SmartBox has been successfully implemented and compiled. Here is a summary of the changes:

## Changes Made

### 1. Build Exclusion
- **[platformio.ini](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/platformio.ini)**: Excluded the new `AutoOtaManager.cpp` from the compilation source filter in the host native unit test environment and the ESP32-C6 test environment. This prevents compile errors on the host because of ESP32-specific secure socket and network library dependencies.

### 2. NTP Time Synchronization
- **[WifiManager.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/WifiManager.cpp)**: Integrated a call to `configTime` with Korea Standard Time (KST, UTC+9) timezone offset and NTP pool servers inside the WiFi station `GOT_IP` event. This synchronizes the device's system time automatically when an external internet connection becomes active.

### 3. Auto-OTA Manager
- **[AutoOtaManager.h](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/AutoOtaManager.h)**: Created the header file defining the `AutoOtaManager` class and hardcoded Synology NAS target URLs for the version index (`version.json`) and firmware binary (`firmware.bin`).
- **[AutoOtaManager.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/AutoOtaManager.cpp)**: Implemented the manager logic:
  - **Scheduler**: Checks time in a non-blocking loop every 30 seconds using `getLocalTime` and triggers `AutoOtaTask` once a day if the current hour is 3 AM KST.
  - **Version comparison**: Runs in a background FreeRTOS task. It sends an HTTPS GET request to the NAS, extracts the remote version field string via manual JSON parsing, and compares it with the hardcoded device version `FIRMWARE_VERSION` ("0.0.1").
  - **Safety Interlock**: Before executing `httpUpdate.update(...)`, it cuts power to the relays via `controller.forceAllRelaysOff()`, sets the state machine to `STATE_OTA_UPDATING` (stopping sensor reading), and delays for 500ms to let contacts separate.
  - **Failure Recovery**: On update failure or network disconnection, the task restores the FSM state back to `STATE_IDLE` so the box continues normal operation when daylight comes.

### 4. Integration & Dashboard Refactoring
- **[WebDashboard.h](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/WebDashboard.h)** & **[WebDashboard.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/WebDashboard.cpp)**: Removed the local duplicate OTA task implementation and refactored the `/api/update-from-nas` endpoint to call `AutoOtaManager::startOtaUpdate(true)` (force manual update).
- **[main.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/main.cpp)**: Registered `AutoOtaManager::init()` in `setup()` and polled `AutoOtaManager::update()` in the main non-blocking `loop()`.

## Verification & Compilation Results

We ran PlatformIO compilation for the ESP32-C6 target environment:
```bash
pio run -e esp32-c6-devkitc-1
```
The build completed successfully:
- **Status:** `SUCCESS`
- **RAM Usage:** 14.0% (45,744 bytes of 327,680 bytes)
- **Flash Usage:** 19.4% (1,274,374 bytes of 6,553,600 bytes)
- **Warning checks:** No unexpected build warnings were produced.
