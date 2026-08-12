---
title: Telemetry alignment implementation plan snapshot
type: proposal
project: smartbox
status: superseded
updated: 2026-08-12
---

# Telemetry Timestamp Alignment, Field Filtering, and Version Bump to 0.1.0 Plan

> **Superseded snapshot:** 완료된 0.1.0 시점 작업 계획입니다. 현재 상태는 [project_status.md](../wiki/project_status.md)를 따릅니다.

This plan implements conditional field filtering and absolute timestamp alignment for InfluxDB telemetry, and bumps the firmware version to 0.1.0.

## Proposed Changes

### Telemetry System

#### [MODIFY] [TelemetryManager.h](../src/TelemetryManager.h)
- Add `wifi_rssi` (`int`) to `TelemetryData` struct to capture WiFi signal strength.
- Add `uploadStartMillis` (`unsigned long`) and `currentEpochMs` (`uint64_t`) to `BatchPayload` struct.

#### [MODIFY] [TelemetryManager.cpp](../src/TelemetryManager.cpp)
- **Field Filtering at Sampling Time:**
  - Record `distance_cm` only if `<= 100.0f`. Otherwise, set to `-1.0f` (sentinel).
  - Record `wifi_rssi` using `WiFi.RSSI()`.
- **WiFi Change Update:**
  - Add non-blocking check every 10 seconds.
  - If WiFi connection state changes or RSSI changes by `>= 5 dBm`, trigger immediate async telemetry upload.
- **Field Filtering at Write Time:**
  - Only write `distance_cm` if `>= 0.0f`.
  - Only write `state`, `state_str`, and `motor_current` if the state is not `STATE_IDLE`.
  - Only write `wifi_rssi` if it is not `0`.
- **Timestamp Alignment:**
  - Set write options to `WritePrecision::MS`.
  - Calculate absolute epoch milliseconds using captured references and set on points via `pointDevice.setTime()`.

### Version Configuration

#### [MODIFY] [SmartBoxController.h](../src/SmartBoxController.h)
- Bump default `FIRMWARE_VERSION` from `"0.0.1"` to `"0.1.0"`.

#### [MODIFY] [git_version.py](../scripts/git_version.py)
- Bump fallback `base_version` to `"0.1.0"`.

### Repository Documentation

#### [MODIFY] [implementation_plan.md](implementation_plan.md)
#### [MODIFY] [task.md](task.md)
#### [MODIFY] [walkthrough.md](walkthrough.md)
- Overwrite previous docs with the plans, tasks, and walkthrough files corresponding to the current version `0.1.0` release.

## Verification Plan

### Automated Tests
- Build the firmware using PlatformIO:
  - `pio run`

### Manual Verification
- Verify the dynamic version print in the build logs: `[git_version] Extracted dynamic version: 0.1.0-g<commit>`.
- Push changes to GitHub.
