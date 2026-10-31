# Battery recovery and power monitoring

Status: source implementation and isolated validation; publication/installation are separate gates. Owner approved automatic resume and existing-hardware-first firmware update on2026-10-31.

## Power policy

The system retains its12V lead-acid profile and default11.3V/3s guard. Warning=cutoff+0.7V, saving=cutoff+0.4V. Noncritical changes qualify for15s and have0.1V exit hysteresis to avoid repeated radio publications near a threshold. Recovery requires max(12.4V,cutoff+0.8V) continuously for300s with fresh valid samples; a gap>2s, invalid sample or voltage dip restarts qualification. These are software policy defaults, not measured SOC or a battery-manufacturer acceptance claim.

A confirmed critical episode preserves the existing single3800ms fail-open stroke and interlock. Invalid/missing INA219 instead isolates immediately, including failure during that stroke; it never returns synthetic12.2V/0mA. Recovery leaves outputs isolated, clears pending movement/median/debounce and enters idle with3s blind time. Fresh normal detection may then run the existing FSM. Recovery alone does not force a lid stroke or prove physical position.

Protection is persisted. A boot whose NVS says protection or logical lid-open starts isolated recovery rather than a calibration stroke, avoiding repeated movement on power failure or OTA reboot. The explicit lab STARTUP_OPEN regression remains. Old NVS lid state is not actual position evidence.

## Savings and update recovery

Normal/warning/saving distance polls remain50/50/100ms. Voltage/current sample200ms normally or moving,1000ms while isolated. Main yields1ms normally/20ms while isolated; network processing yields100/1000ms. No scheduled night sleep is introduced. After the5min provisioning window, connected Wi-Fi modem sleep is enabled; this preserves the MQTT connection rather than entering deep sleep.

Telemetry periods are30/60/120/300s for normal/warning/saving/protected-or-recovering. Qualified phase changes publish promptly. Authenticated transport, watchdogs, relay100ms+100ms interlock, motor timeout and16MB OTA partition remain.

OTA starts only after30s fresh stable recovery-level voltage and no motor/protection. Admission is checked again before flash. The loop observes power during OTA without actuating; failed/no-update exit cannot clear protection. Existing TLS CA/authentication/redirect refusal and previous bootable-slot behavior are retained. No signature/health rollback feature is claimed beyond existing implementation.

## Telemetry and alerts

Existing smartbox/telemetry and retained smartbox/status carry power_schema1, power_sensor_valid, power_state_code, power_sample_age_ms, power_report_interval_s, power_recovery_remaining_s/count, minimum/recovery voltage and admission flags. Codes: NORMAL0, WARNING1, SAVING2, PROTECTED3, RECOVERING4, SENSOR_FAULT5, UNKNOWN6. No new MQTT principal, topic permission or credential is required. Invalid voltage is null; old raw voltage is not reused as current valid data.

HA discovery adds five power sensors and one measurement-fault binary sensor (26 entities total). The existing voltage entity uses live telemetry and availability from both transport and measurement quality. Motor current remains MOTOR_BRANCH; no total battery Wh, charge current or percentage is inferred before wiring/whole-system measurement.

The NAS observer queries only existing Influx telemetry with the existing collector identity, bounds input, rejects redirects and distinguishes source unavailability, stale telemetry, legacy firmware and power faults. Warning/saving alerts qualify60s; critical faults are prompt and deduplicated. Local owner DSM notifications have a persisted retry queue. OFFLINE means no fresh telemetry beyond2x the announced period+30s, not a confirmed battery/electrical diagnosis. No actuator/control publishing exists in the observer.

[Operator installer](../ops/install_power_monitor.py) verifies the exact captured source, validates the existing query before writing, creates only its dedicated service/config/state and rolls back those new files on failure. It does not restart the broker/API/collector/HA. External test email and account/security changes are excluded. [Grafana dashboard export](../ops/power_dashboard.json) needs an existing authenticated import; a prepared file is not operating dashboard acceptance.

## Validation and field boundaries

Native production-component regressions include permanent shutdown recovery, dip/gap/clock wrap, sensor failure, rejected stale/under-threshold commands, independent timers, no recovery pulse, old relay/stall/OTA contracts and warning chatter. Python checks cover clock/source/quality/legacy-cohort rejection, stale-period handling, deduplication and actual installation rollback under isolated files/service fixtures. C6 compilation uses dummy local provisioning; that image must never be published.

Before operating OTA, preserve the prior artifact/metadata, require reviewed CI publication and authenticated HTTPS content/version readback, observe fresh adequate voltage and send at most one owner-authorized trigger_ota. Installation/new boot/new schema and automatic recovery need actual observation. Physical lid movement and energy savings require separate field evidence; no unattended actuator test is permitted.
