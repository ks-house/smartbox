# SmartBox Field Recovery Kit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Produce a private, offline Windows USB recovery kit for the existing0.2.1 correction without changing deployed firmware.
**Architecture:** Existing Actions secrets -> exact-commit build -> CMS AES256-GCM encrypted artifact -> owner-private local decryption; guarded Windows USB tools plus Korean procedure.
**Tech Stack:** Python3/PlatformIO in WSL+Actions, OpenSSL CMS, PowerShell5.1/7 and pinned official esptool.
**Spec:** [field recovery design](../specs/2026-11-01-field-recovery-kit.md)

## Global Constraints
- No unencrypted credential-bearing Actions artifact or Git secret, no operating publication/unhold, no device command this task.
- USB app0/otadata only, same16MB layout; preserve NVS, bootloader, second slot and exact pre-write sectors. Unknown chip/security/layout abort.
- Stable physical USB-only board power and disconnected motors are field prerequisites, not remotely verified claims.

## Review Focus
- Wrong8MB spare board or encrypted device must not be written.
- Invalid/partial backup, mismatched hashes/MAC or unknown partition layout must stop before writes.
- Default invocation must never connect/reset/write a physical device.
- Failed verification must not reset into the app or report success.
- Production provisioning must be encrypted before artifact upload and excluded on any failure.

### Task1: Build/encrypt exact private firmware
Files: scripts/build_field_recovery.py, test/test_field_recovery.py, .github/workflows/deploy.yml. Add failing secret/header/metadata/encryption tests; implement ephemeral header and authenticated-encrypted package; verify dummy inputs locally. Add explicit dispatch-only build job with expected commit + public recipient and no deployment credentials/path.

### Task2: Guarded Windows USB recovery
Files: tools/field_recovery/Recover-SmartBox.ps1, FieldRecovery.psm1, Capture-Serial.ps1 and test/field_recovery.Tests.ps1. Red tests for dry-run/no write, wrong chip/flash/security/layout, backup/verification failures, NVS preservation and exact restore; implement only reviewed offsets and durable private backups; run Windows5.1 and CI pwsh tests.

### Task3: Assemble and document field kit
Files: wiki/field_recovery.md, wiki/index.md, wiki/project_status.md, wiki/log.md, docs/WIRING_GUIDE.md. Official board/esptool/INA219 references, Korean equipment and safe field sequence. Verify authentic portable tool and private artifact identity; assemble owner-private offline folder, validate hashes and dry-run without touching hardware. Review, relevant suite/C6/CI, update PR22 and issue19 with explicit installation pending.
