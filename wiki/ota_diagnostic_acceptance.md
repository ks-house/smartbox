# Deployed OTA diagnostic failure and recovery acceptance

Issue7's remaining gate is an installed-device diagnostic failure followed by
successful recovery, rather than another firmware publication.

## Bounded manifest-only failure

The helper renders a temporary nginx rule scoped to one exact original IPv4
peer, one existing OTA username and /version.json. A server-clock map expires
after at most120 seconds even if cleanup is interrupted. It rewrites only that
manifest request to a verified missing upstream path with rewrite/break, preserving
the existing auth/access/proxy phases. It does not return protected data before
password validation, alter TLS, permit anonymous access or touch /firmware.bin.

The production renderer uses realip_remote_addr and msec. Query-provided peer/
clock values exist only in a private Unix-socket fixture. Real native nginx52 cases
verify correct/wrong/anonymous authentication, other users/peers/paths, encoded
manifest paths, expiry, all mutating-method refusals and Authorization stripping.
Initial isolated fixture syntax/module-shadow defects were corrected before any
live policy or device command.

## Operating admission

Before a live test, require: current authenticated fresh IDLE telemetry and deployed
bounded OTA fields; current firmware version and served protected artifact match
the accepted e0323a0 image; one current broker-observed peer; unchanged stable
nginx guard hashes; full nginx candidate validation; private owned-file creation
with exact cleanup and an independent bounded watchdog. No broad public download
exception or credential rotation is part of this test.

A trigger_ota command only performs a no-update check when the served manifest
matches the installed version in the existing firmware. While the narrow failure
is active it must report FAILED/VERSION/HTTP404 with the next attempt count.
After removal/expiry, the next check must report UP_TO_DATE/VERSION/HTTP200.
Both must arrive as fresh nonretained telemetry with increasing millis timestamps,
IDLE FSM, no updater error and no queued request. Exactly two attempts are allowed.

The acceptance helper refuses missing/extra diagnostic fields, queued or firmware
phase, bool-as-number, stale/reset timestamps, wrong HTTP/state and skipped/repeated
attempt counts. No actuator command or firmware transfer is intended. Monotonic
continuity is a software observation, not a physical lid/electrical qualification.

## Current validation boundary

Four Python contract tests pass; the Docker nginx fixture is an explicit opt-in.
An actual NAS native nginx isolated52-case fixture passes using dummy credentials
and private Unix sockets. Operating fault/application, two actual device observations
and exact cleanup remain pending. Native13/C6 checks are run with dummy provisioning;
no firmware publication is needed for this operational helper.
