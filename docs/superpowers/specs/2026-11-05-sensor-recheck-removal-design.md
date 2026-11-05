# Approved SENSOR_RECHECK removal design

Owner: explicit deletion request followed by approval after concrete risk disclosure on2026-11-05. Implement full removal, not renaming or another startup exception.

Delete the operating state, requalification timer and invalid/stale sensor blocking of normal manual/automatic movement. Preserve invalid diagnostics and previously verified low-voltage admission/latches. Keep stroke timing, relay interlock, obstacle, protected charging-close and authenticated/isolated OTA contracts. A missing sensor no longer prevents ordinary motion; electrical current/voltage protection cannot be evaluated while measurements are unavailable. This scope and risk are approved.

See [implementation and evidence](../../../wiki/sensor_recheck_removal_20261105.md). Publishing and actual installation remain separate acceptance steps.
