> Historical0.2.7 contract, superseded in0.2.8 source by [approved full removal](sensor_recheck_removal_20261105.md). Device installation is separate.

# SENSOR_RECHECK remaining contract (2026-11-05)

The owner reports another SENSOR_RECHECK and asks why it remains after removal requests. This is a source-confirmed explanation, not fresh runtime fault attribution.

## What was actually shipped

Installed acceptance on November 4 was 0.2.7-g798af34. Both opening and full closing were owner-confirmed then. The enum/state and fail-closed sensor path were not deleted. The change deferred one narrowly matched positive INA219 full-scale cohort for less than 500 ms of an already powered normal OPENING/CLOSING stroke. Reporting this as a complete removal would be incorrect.

The exception requires driver 1, configuration 0x399f, calibration 4096, RANGE fault, mask 6, finite bus above 0 and at most 16 V, positive shunt within 0.01 mV of 320 mV and current within 0.2 mA of 3200 mA. At 500 ms the controller forces a new read. Invalid data after the deadline stops immediately. Different range masks/signs/values do not qualify, even within the startup window.

## Remaining triggers

- INA219 I2C read failure; configuration/calibration mismatch or missing sensor initialization.
- Nonfinite/invalid power, voltage outside the controller's accepted 0..16 V interval, shunt/current range rejection.
- A range error outside normal OPENING/CLOSING, or outside the exact startup exception.
- Expired valid data (maximum sample age 2000 ms), when observed by the protection path.
- A prior sensor fault still awaiting fresh valid samples spanning 1000 ms. Every further invalid sample resets this stability qualification.

Without a persistent battery hold, servicePower isolates the relays and enters SENSOR_RECHECK. On qualification it returns to IDLE without repeating the interrupted stroke; automatic detection must rearm after measured absence. Genuine low-voltage persistent protection is separate: cutoff <=11 V for 3 s and warning <=11 V continuously for 300 s. Removing a label alone would not alter the stop.

## Current incident evidence limit

Read current source, index and append-only log. Existing root-maintenance status returns sudo password required; browser runtime cannot start under the current host security environment. No fresh November 5 device fault code, raw cohort, timestamp or installed-version readback was obtained. Do not infer this incident is the same as the November 4 startup saturation. No physical command, reboot, OTA, safety bypass or operating setting change was performed.
