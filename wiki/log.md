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

## [2026-10-16] code | Prepare combined MQTT accounts and protected OTA publication

- Owner authorizes SmartBox/Telegraf in Smart Gatekeeper328. New non-retained telemetry and retained632f004 version observed read-only; no actuator command. Existing anonymous credential-bearing firmware HEAD returns200, so fresh credentials require origin protection first.
- Added root-plan/hash-checked nginx origin guard with private backup, whole-tree candidate validation,28 Unix-socket HTTP behavior cases, original-peer CIDR mapping, preserved generated Web Station includes and bounded rollback of new files. Initial NAS invocation stopped before operating writes; confirmed both guard files absent. Config-prefix RED/GREEN and retained diagnostic reporting fix prepared; retry pending.
- Added external HEAD/GET refusal gate before real-credential publication and PR placeholder native/target checks with no deployment. New bounded real MQTT credential preflight prevents empty/placeholder anonymous builds.
- Floating pioarduino HEAD55.3.312 failed: IncompatiblePlatform, Core6.1.19 versus required>=6.2.0. Last successful operating CI31599873352 records55.3.311/dd01e49. Pinned both target envs to full dd01e49; native13 PASS and target build PASS (RAM62156B/flash1342740B), preserving relay/FSM/16MB/OTA behavior.
- Four gate tests and two actual nginx regressions PASS; fixture covers28 HEAD/GET cases. No new credentials, firmware publication/installation, broker activation or physical success claim. Local secrets are placeholder ignored files. New wiki linked and current manuals synchronized; final links/whitespace validation precedes publication.

## [2026-10-16] test | Install OTA origin guard after config-prefix correction

- Owner reran hash-pinned guard05 with interactive sudo. Exact private receipt confirms full native nginx config validation,28 Unix-socket allow/refuse cases, local strict-TLS firmware/version checks and two root-owned include installation. Original generated Web Station files remain intact. Credentials unchanged; external refusal is explicitly not yet verified.
- Candidate CI gains manual verify-origin as a read-only default; privileged publication requires main push or explicit publish-firmware, plus successful native/origin gates. No real firmware/credential publication has occurred during this candidate work.
