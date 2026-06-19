# Tasks - Auto-OTA System Implementation

- [x] Exclude `AutoOtaManager.cpp` from host/native tests in `platformio.ini`
- [x] Configure NTP (KST timezone) on WiFi IP connection event in `WifiManager.cpp`
- [x] Create `AutoOtaManager.h` declaring the manager and URL constants
- [x] Create `AutoOtaManager.cpp` with NTP scheduling and the background HTTPS GET version comparison & update routines
- [x] Refactor manual update handler in `WebDashboard.cpp` to use `AutoOtaManager`
- [x] Integrate `AutoOtaManager::init()` and `AutoOtaManager::update()` into `main.cpp`
- [x] Compile and verify using native unit tests and target build commands
