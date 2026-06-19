# Implementation Plan - Auto-OTA (Automatic Over-The-Air) Update System

This plan outlines the implementation of an autonomous background OTA firmware update system for the ESP32-C6-based SmartBox. The system will synchronize time with NTP, poll the time in a non-blocking scheduler, check for new firmware updates on the Synology NAS securely via HTTPS every night at 3:00 AM, and perform a safe update after isolating all relays.

## User Review Required

> [!IMPORTANT]
> **Safety Interlock Sequence:**
> Before any firmware flashing begins, the system will explicitly isolate the physical relays to prevent short-circuiting or unintended motor behavior.
> - Call `controller.forceAllRelaysOff()` to switch relays (`GPIO 6, 7, 8`) to high-impedance mode.
> - Transition state machine to `STATE_OTA_UPDATING` to halt sensor/actuator polling.
> - Wait `500ms` for physical relay contacts to fully separate before starting the update.

> [!NOTE]
> **Refactoring Web Dashboard OTA Routine:**
> To maintain a clean and modular architecture, the existing manually triggered OTA routine in [WebDashboard.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/WebDashboard.cpp) will be migrated to the new `AutoOtaManager` class. The Web Dashboard API will delegate execution to `AutoOtaManager::startOtaUpdate(true)` (force update).

## Proposed Changes

### 1. Build and Dependency Configuration

#### [MODIFY] [platformio.ini](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/platformio.ini)
- Exclude the new `AutoOtaManager.cpp` from the source filter for `[env:native]` and `[env:esp32-c6-test]` environments to prevent compilation errors during host-side testing (due to `<WiFiClientSecure.h>` and `<HTTPClient.h>` dependencies).

---

### 2. Time Synchronization

#### [MODIFY] [WifiManager.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/WifiManager.cpp)
- Call `configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov")` inside `onWiFiEvent` when the `ARDUINO_EVENT_WIFI_STA_GOT_IP` event is received.
- This configures the timezone to Korea Standard Time (KST, UTC+9) and starts background NTP synchronization automatically.

---

### 3. Auto-OTA Architecture & Scheduler

#### [NEW] [AutoOtaManager.h](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/AutoOtaManager.h)
- Declare the `AutoOtaManager` class.
- Define target URLs:
  - `VERSION_URL` = `"***REMOVED***"`
  - `FIRMWARE_URL` = `"***REMOVED***"`
- Expose static methods:
  - `init(SmartBoxController& controller)`
  - `update()`
  - `startOtaUpdate(bool force)`

#### [NEW] [AutoOtaManager.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/AutoOtaManager.cpp)
- Implement `init()` to store the controller reference.
- Implement `update()` to:
  - Return early if WiFi is not connected.
  - Check current time using `getLocalTime(&timeinfo, 0)` every 30 seconds (non-blocking).
  - Verify if it is 3:00 AM (KST) and if the update check hasn't run yet today.
  - Trigger `startOtaUpdate(false)` (version-checked update).
- Implement `startOtaUpdate(bool force)` to spawn a FreeRTOS background task `otaTaskFunction` with a 16KB stack size.
- Implement the FreeRTOS task function:
  - If not forced, fetch `version.json` via HTTPS GET using `WiFiClientSecure` and `HTTPClient` (with `setInsecure()`).
  - Parse `version.json` manually using a simple string scanning function to get the remote version.
  - Compare the remote version with `SmartBoxController::FIRMWARE_VERSION` ("0.0.1").
  - If a new version is detected (or if `force` is true):
    1. Call `controller.forceAllRelaysOff()`.
    2. Transition controller to `STATE_OTA_UPDATING`.
    3. Wait 500ms for contacts to fully separate.
    4. Call `httpUpdate.update(client, FIRMWARE_URL)`.
    5. Handle successful update with `ESP.restart()`.
    6. On failure, log the error, transition controller back to `STATE_IDLE`, and delete the task.
  - If no update is needed, log and end the task.

---

### 4. Integration & UI update

#### [MODIFY] [WebDashboard.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/WebDashboard.cpp)
- Remove `nasOtaTaskFunction` from `WebDashboard`.
- Refactor the `/api/update-from-nas` endpoint to call `AutoOtaManager::startOtaUpdate(true)` (force update).

#### [MODIFY] [main.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/main.cpp)
- Call `AutoOtaManager::init(controller)` in `setup()`.
- Call `AutoOtaManager::update()` in `loop()`, guarded by `!controller.isOtaMode()`.

---

## Verification Plan

### Automated Tests
- Run native unit tests via:
  ```bash
  pio test -e native
  ```
- Run ESP32-C6 compilation to verify no build errors:
  ```bash
  pio run -e esp32-c6-devkitc-1
  ```

### Manual Verification
1. **Boot and Wi-Fi Connection:** Verify that ESP32-C6 connects to Wi-Fi.
2. **NTP Sync:** Check serial logs to confirm that time is successfully synchronized (e.g. log output showing synchronized local time).
3. **Web Dashboard OTA:** Click "Fetch & Update" on the Web Dashboard to verify that the manual OTA flow still works by invoking `AutoOtaManager::startOtaUpdate(true)`.
4. **Auto-OTA Timing Check:** Log the mock or real time changes and verify the 3:00 AM scheduler triggers the background task.
5. **Version Comparison:** Confirm the task performs HTTPS GET, parses `version.json`, logs the version, and decides to update (if version is different) or skip (if version is identical).
6. **Safety Lock Verification:** Confirm that if update runs, all relays are immediately shut off and the system enters `STATE_OTA_UPDATING` status.
7. **Failure Recovery:** Simulate a failed connection (e.g. by setting an invalid URL or turning off WiFi during download) and verify that the system recovers to `STATE_IDLE` and logs the error.
