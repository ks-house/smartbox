# Tasks - Configurable Auto-OTA Scheduled Hour

- [x] Add `otaHour` field to `BoxConfig` in `SmartBoxController.h`
- [x] Save and load `otaHour` in NVS Preferences in `ConfigManager.cpp`
- [x] Read dynamic `otaHour` in the scheduler of `AutoOtaManager.cpp`
- [x] Expose `otaHour` in Web Dashboard API endpoints (`/api/status` & `/api/config`) in `WebDashboard.cpp`
- [x] Add UI field and event script handlers for `otaHour` in Web Dashboard HTML page in `WebDashboard.cpp`
- [x] Compile and verify the build for the target ESP32-C6
