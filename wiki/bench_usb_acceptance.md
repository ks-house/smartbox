# SmartBox USB bench acceptance —2026-11-02

## Observed

- Owner confirms this is SmartBox, motor12V and external board5V disconnected, USB-only power; later confirms INA219 is not attached.
- CH343 UART enumerates COM5. esptool reports ESP32-C6/QFN40 rev0.2,16MB flash, manufacturer68/device4018, Secure Boot and Flash Encryption disabled. Same MCU MAC matches native USB/JTAG COM6 after cable transfer. Raw MAC and original NVS are private evidence only.
- Original bootloader is C6/16MB. First high-baud attempt stops during the next connection, before backups/writes. At115200 default-reset and subsequent no-reset connections are both demonstrated.
- Sector-rounded original app0 range plus first64KiB/otadata are privately saved in backup folder20261102-181348-79d173f1. Firmware and otadata are written and explicitly verified. First0xe000 bytes (bootloader/table/NVS) compare byte-for-byte unchanged.
- Installed image0.2.1-g4a57756, commit4a57756068874b30ab20a8d539c0cd23d5bd8543,1403584B, SHA2566f58191c02128af4e98dba155179bbdac0314f35dae1a7b7323136db60b7cff6.
- Owner moves the cable to the second native USB port. Actual runtime prints SYSTEM Firmware0.2.1-g4a57756 plus five POWER-DIAG observations over the30s capture. driver1/fault10/config0/calibration0/valid0 is consistent with the owner-confirmed sensorless bench. The loop remains responsive and retries initialization.

## Tool correction

Separate esptool CLI processes initially synchronize at115200. A prior high-baud command can leave a live stub at460800, making the next no-reset115200 handshake fail. The connected board reproduces high-baud failure and115200 success; transport fixtures reproduce the speed contract RED→GREEN. Both wrapper and module now default to and require115200. Unsupported speeds stop before any device call.14 Windows guard cases pass. No production firmware logic/image change is needed.

## Remaining acceptance

USB write/readback and application boot are complete. Actual INA219 I2C/voltage/current readings,12.4V/300s charged recovery, physical relay/motor operation and field network/OTA acceptance are not established by a bare board. No motor command was sent, operating manifest remains held and issue19 remains open. Reconnect using the [field procedure](field_recovery.md); remove USB before restoring external board5V. Preserve the original backup privately.


## 2026-11-03 actual0.2.2 USB write acceptance

- Owner confirms the connected board is SmartBox with motor12V and external5V disconnected, USB only. COM5 CH343 identity and actual ESP32-C6 rev0.2/base MAC match the previous private MCU receipt. All21 immutable prepared-kit file hashes match; actual image0.2.2-gdb96897/1410336B/SHA25686fbf0dfee64a245cafd13839ecb05e2c1c435a93ea1efd7f3affb4d54427bb9.
- One guarded Apply invocation at115200 completes state=written_verified. Actual tool validates C6/16MB/security/layout and backs up app0 write span1413120B, first64KiB and otadata before writing. Private backup20261103-181213-95755dff hashes match; old0.2.1-g4a57756 literal remains in the preserved app. New firmware/otadata explicitly verify; original prefix0..0xe000 compares identical (bootloader/table/NVS). No app1 write or erase-all; vendor bounded internal retry remains possible.
- Board intentionally remains in ROM download mode. Native USB cable transfer is requested so actual0.2.2 application boot can be read. Boot/new loop/sensor/network/physical field acceptance are pending at this write checkpoint. No motor or OTA command; operating NAS release unchanged.
- Required documentation checks this turn: native57 PASS and default C6 public-dummy build PASS. Generated version header restored. Local dummy build is not the installed image and is not published.


## 2026-11-03 actual0.2.2 boot acceptance

- Owner moves the cable to nativeUSB and later confirms this is the bare board without INA219. COM6 VID303A/PID1001 parent identity matches the same private MCU MAC. Read-only30s capture prints SYSTEM Firmware0.2.2-gdb96897, initialization finished/FSM loop and relay-isolation log. Five POWER-DIAG observations driver1/fault10/config0/cal0/valid0 are expected for the owner-confirmed sensorless bench. No synthetic voltage or connected-sensor failure diagnosis.
- USB transfer produces one initial USB_UART_HPSYS boot/reset marker; after the expected firmware line, zero further ROM/reset/fatal/panic markers occur in the captured window. Existing TWDT already initialized message occurs once; effective watchdog configuration is not independently established by this observation. Do not describe this bounded capture as long-term/field SLO proof.
- Preferences load retains configured parameters including cutoff11.3V, actuator3800ms and existing stall5000mA. Programming prefix comparison preceded boot; mutable runtime Preferences are not claimed byte-identical forever. New release policy is12.0V/300s under the observed cutoff.
- Actual USB write/readback/new-version boot and continued bench execution are accepted. Field reinstatement, connected INA219 voltage, physical close/charging, HA buttons, network/OTA acceptance remain pending. Motor/OTA commands remain0; prior image backup preserved. Remove USB before restoring external board5V. The operating OTA origin was not changed.
