# SENSOR_RECHECK removal (2026-11-05)

## Scope and explicit approval

The owner explicitly requested deleting the feature after recurrence on0.2.7. The first production-edit attempt was rejected by automatic review because removing normal-motion sensor gates also permits manual AND ultrasonic automatic operation with disconnected/invalid power measurement. After that concrete electrical-risk explanation, the owner explicitly replied approval. The same scope was then allowed. No approval bypass or security/SSH change occurred.

##0.2.8 source contract

- Delete STATE_SENSOR_RECHECK, its MQTT state name, state transitions/alarm, one-second sensor requalification timer and normal-motion admission/stop gates for invalid/stale measurements.
- Delete the narrow500ms sensor exception and its pending latch. Existing normal current-stall startup grace stays separate.
- Invalid power stays invalid/raw, never a fabricated voltage/current. SENSOR_FAULT remains a measurement diagnostic, not an operating FSM hold. Recovery to a valid diagnostic is immediate.
- Normal manual and ultrasonic automatic motion can proceed with missing/invalid power readings, bounded by existing stroke time and relay/obstacle rules. With no usable sensor, actual undervoltage/current cannot be judged. This limitation was explicitly approved.
- A previously verified low or overvoltage still denies new normal admission; invalid readings cannot erase that known value or an existing battery latch. Valid <=11V/3s protection, <=11V/300s warning and12V/300s recovery remain. Invalid/gapped data cannot qualify their timers.
- Protected charging-close still requires valid readings and retains its voltage/current/TTL/once-per-boot restrictions.
- Normal/forced OTA still requires fresh qualifying power; explicit sensor-fault recovery keeps existing known-low exclusion, expiry, once-per-boot and atomic output isolation. Recovery now enters from IDLE rather than a deleted state.
- Retired wire field power_sensor_recheck_remaining_ms remains fixed0 solely for N-1 schema compatibility. No control timer exists. Old HA history is not erased.

## Verification and evidence boundary

Four removal behaviors were RED first. Retired tests requiring the deleted hold were replaced with tests for sustained invalid normal motion, fresh immediate admission, preserved known-low latch, full ultrasonic cycle, stroke timeout under repeated requests, and unchanged protected-close/OTA isolation. Immediate valid diagnostic recovery also reproduced RED before correction. Native77 and actual OTA integration21 pass; full Python144 total/135executed/9 explicit opt-in skips passed. Independent reviewer reports no outstanding Critical/Important findings.

Candidate0.2.8 is a source change, not evidence that the remote device has installed it. Browser runtime launch failed; existing root maintenance status returned sudo password required. No live November5 fault attribution, actuator command or reboot was performed. Publication, installation and physical acceptance must each be recorded separately.
