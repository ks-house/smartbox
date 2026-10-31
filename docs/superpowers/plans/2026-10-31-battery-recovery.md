# SmartBox battery recovery Implementation Plan

> For agentic workers: use superpowers:executing-plans inline. Track RED/GREEN and decisions in the private progress ledger.

**Goal:** Automatically recover from low-voltage protection with observable bounded power savings and install the reviewed firmware.
**Architecture:** A pure BatteryPolicy owns measurement validity and hysteresis. SmartBoxController retains actuator safety; network/OTA/monitoring consume an immutable policy snapshot.
**Tech Stack:** C++/PlatformIO/pioarduino ESP32-C6, native Unity, Python stdlib, MQTT/Influx/Grafana/HA.
**Spec:** ../specs/2026-10-31-battery-recovery-design.md

## Global Constraints
Preserve GPIO4/5/6/7/8/22/23, Active-Low interlock100ms+100ms,3800ms actuator timeout, default_16MB.csv, authenticated OTA and previous bootable artifacts. No fake sensor values, forced physical tests or general backup461 work. Use WSL Linux tools and never publish dummy local binaries.

## Review Focus
Freshness gaps/wrap must not qualify recovery; bad INA219 reads must not become nominal data; occupied/stale distance must not cause an immediate recovery stroke; failed OTA must retain power inhibition; stale/source-down monitoring must not be called battery failure.

### Task1: Power policy and safe controller recovery
Files: src/BatteryPolicy.h/.cpp, HardwareInterface.h, Esp32Hardware.h/.cpp, SmartBoxController.h/.cpp, ConfigManager.h/.cpp, main.cpp, test/test_native/test_main.cpp.
Interfaces: BatteryPolicy::sample(uint32_t,float,bool,float), snapshot(uint32_t), restoreProtection(); controller powerSnapshot(), canStartOta(), restorePowerProtection().
- [ ] Add failing production-component tests for stable recovery, transient/gaps/wrap, invalid reading, per-instance timers, no recovery pulse and command denial.
- [ ] Implement pure policy and INA219 validity; integrate controller without changing legacy interlock/stall/critical stroke.
- [ ] Persist protection and replace main's uncertain boot calibration with isolated recovery.
- [ ] Run full native suite; record results and commit.

### Task2: Savings, telemetry, OTA and monitoring
Files: src/MqttManager.cpp/.h, TelemetryManager.cpp, PowerManager.cpp, WifiManager.cpp, AutoOtaManager.cpp, ops/power_monitor.py, ops/power_dashboard.json, test/test_power_monitor.py; existing manuals/wiki.
Interfaces: immutable BatterySnapshot with valid/state/remaining/report interval/OTA readiness; schema1 numeric telemetry fields; monitor evaluate(sample,now) separates source/device/quality/power.
- [ ] Write RED tests for monitoring stale/source/invalid/dedup/recovery and OTA/sampling integration gates.
- [ ] Implement adaptive cadence and quality/HA discovery, safe OTA admission and no low-power scheduled reboot.
- [ ] Implement read-only NAS observer/dashboard using existing credentials and owner-local alarm path; preserve credentials and configuration boundaries.
- [ ] Run native/Python/target build and OTA auth regressions; update wiki/index/log/manuals; commit.

### Task3: Review, publication and installed acceptance
- [ ] Review exact branch with fresh reviewer; fix important findings with reproducing tests, rerun relevant full gates.
- [ ] Verify GitHub auth, create UTF-8 PR, read back title/body, attach PR, require checks, merge approved firmware change.
- [ ] Preserve prior firmware/metadata; verify protected CI publication and authenticated HTTPS bytes/version.
- [ ] Install scoped monitoring after isolated tests; verify actual status/dashboard without external test email.
- [ ] If fresh device voltage and idle state permit, send one authorized OTA trigger; otherwise preserve published recovery route and explain actual blocker.
- [ ] Verify actual installed version/new boot/schema/health and continuous auto-recovery observation separately from physical movement. Record outstanding energy/physical evidence.


## Follow-up: independent read-only monitoring bootstrap

The first operating OTA did not produce installed acceptance and the owner cannot inspect the field device now. Keep the no-resend intent and preserve both artifacts; device restoration needs new evidence. Independently reproduce stale-legacy misclassification and schema1-only installer blocking. Add explicit --bootstrap for healthy Influx with legacy/offline device while preserving default schema1 and source-error rejection. Validate dedup/no voltage claim, run relevant Python/native/C6 checks and a fresh narrow review, then deploy only the NAS observer from pinned source. A source-only merge must cancel its redundant main firmware publication before the build job starts; the published MCU artifact stays exactly7a55/12c42958. Verify active service, fresh source classification, local owner notification and unchanged collector/artifact/readiness. No device, account, ACL or TLS changes.
