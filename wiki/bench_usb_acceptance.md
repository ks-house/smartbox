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
