# Implementation Plan - Configurable Auto-OTA Scheduled Hour

This plan details the addition of a configuration property `otaHour` to persist the daily Auto-OTA trigger hour in the ESP32 Preferences, allow API edits via `/api/config`, and expose a new input field on the Web Dashboard interface.

## User Review Required

> [!NOTE]
> **Input Bounds Validation:**
> The Auto-OTA hour setting will be restricted to a range of `0` to `23` hours. Inputs outside of this range will fallback automatically to the default value of `3` AM to ensure the scheduler remains valid and safe.

## Proposed Changes

### 1. SmartBox Controller Configuration

#### [MODIFY] [SmartBoxController.h](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/SmartBoxController.h)
- Add `int otaHour = 3;` to the `BoxConfig` struct.

---

### 2. Configuration Management

#### [MODIFY] [ConfigManager.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/ConfigManager.cpp)
- **`loadConfig()`**: Load `otaHour` from NVS Preferences using `prefs.getInt("ota_hour", config.otaHour)`. Include a self-healing range check (defaulting to 3 if out-of-bounds).
- **`saveConfig()`**: Write `otaHour` to Preferences using `prefs.putInt("ota_hour", config.otaHour)`.
- Update config log output to print `ota_hour`.

---

### 3. Auto-OTA Scheduler Integration

#### [MODIFY] [AutoOtaManager.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/AutoOtaManager.cpp)
- Update the schedule condition in `AutoOtaManager::update()` to check `timeinfo.tm_hour == controllerPtr->getConfig().otaHour` instead of the hardcoded value `3`.

---

### 4. Web Dashboard UI and API Endpoints

#### [MODIFY] [WebDashboard.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/WebDashboard.cpp)
- **`/api/status` endpoint**: Add `"otaHour"` key-value pair into the `"config"` block of the returned JSON string.
- **`/api/config` endpoint**: Support reading the `otaHour` GET parameter, validate it is between 0 and 23, assign it to config, and write to NVS.
- **`INDEX_HTML` string**:
  - Add an input group for "⏰ Auto-OTA Hour (0-23)" under the "Sensitivity Calibration" settings card.
  - In Javascript, fetch and pre-fill `cfgOtaHour` on dashboard load.
  - Include `otaHour` value in the GET query request string when the user clicks "Save Settings".

---

## Verification Plan

### Automated Tests
- Compile target firmware:
  ```bash
  pio run -e esp32-c6-devkitc-1
  ```

### Manual Verification
1. Open the Web Dashboard page and verify the new "Auto-OTA Hour" field defaults to `3`.
2. Change the hour to `4` and click "Save Settings".
3. Check the Serial logs to confirm that `[CONFIG]` outputs `ota_hour=4`.
4. Reboot the ESP32 and check the Serial log to confirm that `ota_hour=4` was loaded successfully from Preferences.
