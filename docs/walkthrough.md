---
title: Telemetry alignment walkthrough snapshot
type: test
project: smartbox
status: superseded
updated: 2026-08-12
---

# Telemetry Timestamp Alignment, Data Filtering, and Version Bump Walkthrough

> **Superseded snapshot:** 과거 InfluxDB 직접 전송 구현과 0.1.0 작업 결과를 보존한 문서입니다. 현재 MQTT-only 구조는 [architecture.md](../wiki/architecture.md)를 따릅니다.

This document summarizes the changes applied to align telemetry data points with their true historical sampling times, filter specific fields according to user rules, bump the firmware version to 0.1.0, and update the repository documentation.

## Changes Made

### Telemetry System

#### [TelemetryManager.h](../src/TelemetryManager.h)
- Added `wifi_rssi` (`int`) to `TelemetryData` struct to track signal strength.
- Added `uploadStartMillis` (`unsigned long`) and `currentEpochMs` (`uint64_t`) parameters to `BatchPayload` struct.

#### [TelemetryManager.cpp](../src/TelemetryManager.cpp)
1. **Timestamp Alignment:**
   - Configured InfluxDB client with millisecond write precision:
     ```cpp
     client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::MS));
     ```
   - Calculated correct epoch milliseconds for each point in `telemetryTaskFunction` using `currentEpochMs` and `uploadStartMillis` offsets.
   - Assigned timestamps via `pointDevice.setTime(pointEpochMs)`.
2. **Data Filtering Rules:**
   - **Sensor Distance:** Only collect/upload if `distance_cm <= 100cm`. Stored as `-1.0f` at sampling time if > 100cm, and omitted from InfluxDB fields at write time if `< 0.0f`.
   - **State:** Only collect/upload if State is not `STATE_IDLE`. Omitted `state` and `state_str` from InfluxDB fields during IDLE.
   - **Motor Current:** Only collect/upload if State is not `STATE_IDLE`. Omitted `motor_current` from InfluxDB fields during IDLE.
3. **WiFi Change Update:**
   - Added a non-blocking check every 10 seconds in `TelemetryManager::update()`.
   - Checks if the WiFi connection status changes or the RSSI changes by `5 dBm` or more to filter noise.
   - Triggers an immediate asynchronous telemetry transmission with `type = "wifi"` upon detection.

### Version Configuration

#### [SmartBoxController.h](../src/SmartBoxController.h)
- Bumped `FIRMWARE_VERSION` definition from `"0.0.1"` to `"0.1.0"`.

#### [git_version.py](../scripts/git_version.py)
- Bumped the fallback `base_version` string to `"0.1.0"` to match.

### Repository Documentation
- Overwrote the repository-wide files in `docs/` with the current plan, task list, and walkthrough:
  - [docs/implementation_plan.md](implementation_plan.md)
  - [docs/task.md](task.md)
  - [docs/walkthrough.md](walkthrough.md)

## Verification

- **Compilation Test:** Executed PlatformIO build (`pio run`). The main environment `esp32-c6-devkitc-1` compiled with **SUCCESS** in 227 seconds.
- **Git Push:** Successfully staged, committed, and pushed all changes to the remote repository.
