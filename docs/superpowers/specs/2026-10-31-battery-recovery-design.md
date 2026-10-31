# Battery recovery, bounded savings and monitoring

Owner approved automatic resume, existing-hardware-first implementation and firmware update on2026-10-31. Continue inline without additional execution-choice prompts.

## Intent and approach
Reuse INA219, relay safety, authenticated MQTT/HTTPS OTA and the existing Influx/Grafana/HA stack. Compared with a hardware-first metering rebuild, this provides recovery and observable protection now. Net battery energy/SOC estimation and hardware changes remain excluded until the actual metering branch is measured. Maintain24/7 sensing in normal/warning/saving modes; no scheduled night sleep.

## Firmware contract
- Preserve legacy11.3V/default3s low-voltage guard and the one bounded fail-open stroke/interlock on a fresh confirmed critical reading. A missing/invalid sensor instead isolates outputs, never fabricates12.2V or drives a motor.
- Pure per-instance BatteryPolicy: NORMAL0, WARNING1, SAVING2, PROTECTED3, RECOVERING4, SENSOR_FAULT5, UNKNOWN6. Relative warning=cutoff+0.7V and saving=cutoff+0.4V (15s qualification/0.1V exit hysteresis); recovery=max(12.4V,cutoff+0.8V) for300000ms continuous fresh valid samples. A gap>2000ms resets qualification. Use uint32 elapsed wrap-safe arithmetic.
- Automatic recovery clears only the protection latch, isolates relays, clears pending movement/median/debounce, enters idle with a fresh3s blind period. It performs no force-open/close; subsequent normal fresh detection uses the existing FSM. Do not infer actual lid position from NVS.
- Persist protection. A boot with persisted protection or old logical lid-open starts isolated protection/recovery rather than automatic calibration. Explicit legacy lab STARTUP_OPEN behavior stays available for regression. This avoids repeated battery-consuming strokes after brownout/OTA.
- Power reading validity includes INA219 I2C success, finite voltage/current and physical sensor range. Recovery works after a sensor becomes available again. Raw motor-branch current is never called total battery consumption.
- Sample voltage/current200ms normally/while moving,1000ms in isolated protection. Distance remains50ms in normal/warning,100ms saving; stop ultrasonic triggering in protection. Telemetry30/60/120/300s for normal/warning/saving/protected-or-recovering. Power-state transitions publish promptly; keep authenticated MQTT, bounded retry and OTA access.
- OTA may start only with fresh valid recovery-level voltage stable30s and no motor/OTA/protection. Recheck before flash. Failed/no-update recovery cannot clear a protection latch. Preserve CA/auth/redirect rejection,16MB OTA partitions, relay isolation and prior bootable slot. No new health/rollback claim beyond actual evidence.

## Monitoring and release
Add schema/validity/state/recovery/report-period fields, retained state and HA discovery to existing telemetry; store numeric fields through existing Telegraf. Reuse existing NAS identities for a read-only observer and Grafana power dashboard, with deduplicated local owner-visible warning/protection/sensor/source/offline/recovery state. Do not send external test email or actuator commands. Source unavailable is distinct from device silence. No SOC percentage or promised runtime without total-current measurement.

Native tests cover low-voltage/transient/recovery resets/time gaps/wrap/invalid measurements/cross-instance behavior, no recovery motor pulse, interlock and OTA admission. Target compile and existing Python origin/auth tests are release gates. Review before merge; preserve previous OTA artifact, publish exact reviewed main with real CI secrets, verify HTTPS identity, issue at most one authorized OTA trigger after fresh adequate-voltage evidence, then verify installed version/reboot/new fields. Do not perform an unattended lid test.
