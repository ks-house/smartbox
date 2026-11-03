# Charging close and forced OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans task-by-task.

**Goal:** Owner-requested one-shot close under latched protection, bounded force OTA and12.0V/300s automatic recovery.

**Architecture:** Existing Controller owns relay/power admission and transient protected-close execution; battery policy owns recovery qualification. MQTT validates maintenance request freshness/replay and reports truthful outcomes. Updater reserves isolation atomically and preserves normal OTA and old bootable slot.

**Tech Stack:** C++17/ESP32-C6/pioarduino/ArduinoJson/PsychicMQTT, native Unity and actual-updater host Python tests, WSL.

**Spec:** [approved design](../specs/2026-11-03-low-power-close-design.md)

## Global Constraints

-11.3V default cutoff unchanged; recovery=max(12.0V,cutoff+0.7V),300000ms uninterrupted.
- Close-only explicit protected mode, sample age<=2000ms, finite voltage/current, cutoff<=V<=16, stop on faults, maximum min(config.actuatorTime,3800ms). Existing100ms+100ms interlock and current guard retained; no open after close until qualified recovery/new detection.
- Force OTA:>=cutoff+0.4V continuously30000ms, no motion, explicit ID/expiry<=120s, no retained/dup/replay, once per boot flash reservation; normal OTA unchanged except selected recovery voltage. Auth/TLS/manifest hold remain.
- No unattended test movement. User's requested actual close remains a separate authorized operation, only after compatible installation and fresh observation. No blind OTA retry or permission expansion.

## Review Focus

- Replayed or delayed commands across reconnect must not close again.
- Sensor invalidity/voltage dip during relay contact delay and motion must isolate without another fail-open stroke.
- Auto recovery during closing cannot race OTA or reopen; protection restored on all terminal paths.
- Forced OTA same-version install cannot weaken TLS/expiry/preflash admission; original slot must remain bootable.
- Report timeout completion as motion finished, never physical lid confirmed.

### Task1: Policy and protected close

Files: src/BatteryPolicy.h/.cpp, src/SmartBoxController.h/.cpp, test/test_native/test_main.cpp.
Interfaces: requestChargingClose() result enum; chargingCloseSnapshot(); force readiness in BatterySnapshot.
- [x] Add behavioral tests for12.0V dwell/dip, protected12V close1stroke, no auto-open, immediate fault/low cutoff stop, current magnitude/time/interlock, reject moving/OTA/emergency, no repeated request. Run native tests RED.
- [x] Implement isolated protected-close substate serviced before general protected handling, return to same protection without emergency opening. Keep previous force_close behavior. Run native GREEN/full baseline.
- [x] Commit explicit files with wiki checkpoint.

### Task2: MQTT command and forced updater

Files: new bounded owner-request validator, MqttManager.cpp/.h, AutoOtaManager.cpp/.h, Controller/BatteryPolicy, host actual-updater fixture tests and native request tests.
- [x] Add request TTL/replay/retained and forced OTA admission/normal/recovery queue compatibility tests; actual updater same-version forced path, final TTL/power denial and failure-state preservation. Watch RED.
- [x] Implement close_for_charging and force_ota commands, HA buttons, terminal/rejection reporting, numeric telemetry. Keep old trigger_ota/recovery contracts. Start forced OTA only after30s above floor,120s uptime and isolated nonmoving protection or idle; per-boot reservation shared with recovery.
- [x] Preserve previous bootable slot and final preflash guards; inspect real updater cancellation boundary and disclose any power-loss limitation. GREEN full suites/build both profiles.
- [x] Commit explicit files and manuals/wiki.

### Task3: Review, publication and installation boundary

- [x] Fresh whole-change reviewer against spec/plan; fix Important findings with RED/GREEN.
- [x] Python/native/UART+nativeUSB C6, docs links/diff/secrets checks. Restore generated header; never publish dummy binaries.
- [x] Update PR22/issue19 in UTF8 with verified readback, push reviewed branch when GH authenticated. Do not unhold operating manifest or claim installed.
- [x] Determine compatible initial installation and current NAS access. If blocked by installed0.2.1 gating, report USB/voltage prerequisite exactly; preserve artifacts.

Execution complete for source/review/private OTA preparation. Actual field installation, physical close and charging acceptance remain blocked/pending; no checkbox above claims those outcomes. User cannot connect USB; see readiness runbook.
