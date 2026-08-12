---
title: Telemetry task snapshot
type: test
project: smartbox
status: superseded
updated: 2026-08-12
---

# Completed telemetry task snapshot

> 이 목록은 0.1.0 시점의 완료 기록입니다. 현재 checkout 검증 결과가 아니며 [project_status.md](../wiki/project_status.md)를 우선합니다.

- `[x]` Add `wifi_rssi` to `TelemetryData` in `TelemetryManager.h`
- `[x]` Implement sampling filter and WiFi change detection in `TelemetryManager.cpp`
- `[x]` Implement field filtering at write time in `telemetryTaskFunction`
- `[x]` Verify compilation of telemetry changes via PlatformIO build
- `[x]` Bump `FIRMWARE_VERSION` to `0.1.0` in `SmartBoxController.h` and `git_version.py`
- `[x]` Copy updated plan, task list, and walkthrough artifacts to `docs/` folder
- `[x]` Verify final compilation of version bump changes
- `[x]` Stage, commit, and push all changes to git
