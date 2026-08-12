---
title: SmartBox wiki log
type: log
project: smartbox
status: active
updated: 2026-08-12
source_of_truth: true
---

# Wiki Log

이 파일은 append-only다. 과거 항목을 현재 사실로 해석하지 않는다.

## [2026-08-12] compile | Establish repository-native Obsidian wiki and current-code baseline

- Added the wiki index, current project status, architecture and knowledge-management governance at repository commit `9f2facb`.
- Separated repository implementation, verification evidence and deployed state; classified reports, prompts and completed plan artifacts as historical snapshots.
- Corrected the agent safety contract for Active-Low relays, Main high-impedance cutoff, direction-neutral HIGH/HIGH and full INPUT isolation.
- Recorded current OTA, MQTT, Wi-Fi, telemetry, startup calibration and CI/CD boundaries without claiming physical-device or deployment proof.
- Prepared reusable relay-interlock and pre-OTA hardware-isolation knowledge for provenance-based promotion to the shared Knowledge Hub.

## [2026-08-12] lint | Validate current documentation and record build environment limits

- Verified relative Markdown links for `README.md`, `AGENTS.md`, `HARDWARE_BOM.md`, `SECURITY_CLEANUP_GUIDE.md`, current `docs/*.md` and `wiki/*.md`; historical `prompts/` and `reports/` retain links to files that existed at the time and are excluded from the current-document link Gate.
- Verified every `wiki/*.md` page is routed from `wiki/index.md`, the Knowledge Hub links resolve and `git diff --check` passes.
- Located PlatformIO Core 6.1.19 at `C:\Users\shcat\.platformio\penv\Scripts\platformio.exe`; it is not on the current process PATH.
- `platformio.exe test -e native` did not compile because Windows could not resolve `gcc` or `g++`; no native test case executed, so this is an environment failure rather than a test assertion result.
- `platformio.exe run -e esp32-c6-devkitc-1` produced no captured output and exceeded both 120-second and 300-second command limits; target build completion remains unverified in this turn and should be rerun locally or in CI.
