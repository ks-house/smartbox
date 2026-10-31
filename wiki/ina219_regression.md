# INA219 regression correction candidate

Status:0.2.1 source candidate, not installed. Deployed0.2.0-g7a55dd8 still reports SENSOR_FAULT. The owner requests remote correction and cannot inspect the field now.

## Evidence and certainty

- Before the update, e0323a0 reported varying12.12V and0.1–0.7mA through the Adafruit continuous reader; these differ from its12.2V/0mA dummy fallback.
- New0.2.0 replaces that path with a custom triggered conversion, strict profile/status/cohort tests and power-down between samples. Remote history contains21 invalid reports/no valid or minimum voltage, while uptime advances and free heap is206900B. Network/data freshness and a global memory failure do not explain those observations.
- Treat the reader change as the primary firmware-regression suspect.0.2.0 exports only the final validity bit, so its exact failing register/branch cannot be determined remotely. A wiring/sensor fault is not established. The correction is a controlled rollback to the field-proven reader plus instrumentation, not a claim of completed live diagnosis.
- A separate defect is confirmed in source: both forced MQTT OTA and periodic HTTPS checks require canStartOta(), which rejects sensor-invalid protection. The active0.2.0 has no alternative uploader/diagnostic-register command. NAS rights or a new manifest cannot change this local gate.

## Measurement correction

Production Esp32Hardware returns to Adafruit INA219 continuous32V/2A configuration0x399F/calibration4096 and the previous50ms Wire timeout. Every voltage/shunt/current read checks vendor success immediately; profile and calibration are read back before/after the sequence. Nonfinite/out-of-range results remain NaN/invalid; no nominal fallback is restored. Failed initialization/I/O re-probes at most once per5s; numeric settling faults do not continually restart conversion. The first conversion gets a bounded2ms initialization wait. Sensor power-down optimization is deferred.

The old triggered model remains only for regression characterization; production does not call it. See [sampler](../src/Ina219Sampler.cpp), [adapter](../src/Esp32Hardware.cpp) and [TI datasheet](https://www.ti.com/lit/ds/symlink/ina219.pdf). Continuous readings are sequential vendor register reads, not a claimed instantaneous power cohort or whole-battery energy measurement.

Added diagnostic fields on existing telemetry: power_read_driver1=continuous baseline, power_read_fault_code, power_read_config and power_read_calibration. Codes0=none,1=config I/O,2=profile mismatch,3=calibration I/O,4=calibration mismatch,5=bus I/O,6=shunt I/O,7=current I/O,8=nonfinite,9=range,10=initialization unavailable. Values describe the last attempted read and may be retained during the5s re-probe backoff. get_power_diagnostics requests one report without GPIO/OTA/configuration changes;0.2.0 does not implement it.

## Explicit future maintenance recovery

Automatic and ordinary forced OTA retain their normal voltage/idle gate.0.2.1 additionally accepts an opt-in trigger_ota with recovery=true only from the existing broker-authorized control role. It requires a32-character lowercase hex request_id, synchronized UTC expires_at within120s, and rejects retained/duplicate delivery. This is not a new identity or permission; a nonce alone grants no authority.

Controller admission additionally requires protected SENSOR_FAULT, outputs already isolated, no motor activity, boot uptime≥120s, recent control-loop sampling attempt, and no remembered verified voltage below cutoff or above16V. An invalid read cannot erase remembered unsafe voltage; it is admission evidence only and is never shown as a current measurement. Unknown battery capacity is explicitly unverified: this is owner-directed emergency maintenance, not healthy-voltage certification or automatic power bypass.

A recovery attempt reserves isolation before network I/O, is consumed once per boot, expires before flashing and rechecks both request TTL and reservation at the final updater call. Failed/no-update/manifest exits retain protection. Queue locks are not held while taking controller/GPIO locks; claimed work becomes busy atomically before unlocking. Only a fresh recovery request may replace unreserved normal/expired queued work. All force/maintenance/motion paths stay inhibited during reservation; no GPIO or contact-delay changes.

These fields/requests are N/N-1 additions. They cannot be retrofitted into the already running0.2.0; no unsupported recovery message or repeated OTA is sent to it. Current release hold and both images remain preserved. Draft status must remain until a genuine compatible admission/deployment path and live readings are demonstrated.

## Verification checkpoint

Local final validation:107 Python tests total,98 executed PASS and9 explicit environment skips; this includes16 actual AutoOtaManager host scenarios. Native46 tests PASS. ESP32-C6 public-dummy build PASS (RAM62324B, flash1355650B). Independent focused reviews have no outstanding Critical/Important findings after reproduced failures were fixed. Hosted CI and actual installation are separate gates. Never publish the local dummy image.
