# Night Sleep Mode Task Checklist

- [x] Modify `SmartBoxController` to add `nightSleepActive` flag and helper methods
- [x] Add `stopWiFi` and `startWiFi` methods to `WifiManager`
- [x] Implement checks in `TelemetryManager` and `AutoOtaManager` to skip execution during night sleep
- [x] Implement `PowerManager` to handle NTP time checking and day/night transitions
- [x] Integrate `PowerManager` into `main.cpp`
- [x] Update `platformio.ini` to exclude `PowerManager.cpp` from native/test builds
- [x] Add unit test verifying the new sleep mode flag and telemetry bypass
- [x] Verify compilation and run tests
