# SmartBox private OTA publishing

GitHub Actions uploads over Tailscale SFTP. SmartBox downloads the manifest and firmware using its existing authenticated HTTPS endpoints.

## NAS and GitHub prerequisites

1. Create a dedicated, ephemeral Tailscale identity for `ks-house/smartbox` (repository ID `1267554045`), the `smartbox-private-ota` GitHub environment, and `refs/heads/main`. Limit this identity to auth-key issuance for `tag:smartbox-github-ota`. Allow that tag only to the verified NAS SFTP address and port. Leave the Gatekeeper identity and access rules in place.
2. Restrict `smartbox-private-ota` to the protected `main` branch. Set the environment secrets `SMARTBOX_TS_OIDC_CLIENT_ID`, `SMARTBOX_TS_OIDC_AUDIENCE`, `SMARTBOX_SFTP_HOST` (literal Tailnet IPv4), `SMARTBOX_SFTP_PORT`, and `SMARTBOX_SFTP_HOST_KEY` (two fields, `ssh-ed25519 <public-key-blob>`). Verify the key against the NAS through a trusted administrative path before setting it. Set `SMARTBOX_SFTP_USER` and `SMARTBOX_SFTP_PASSWORD` for a SmartBox-only SFTP account with no shell.
3. Set `SMARTBOX_SFTP_TARGET_DIR`, `SMARTBOX_SFTP_STAGE_DIR` and `SMARTBOX_SFTP_ROLLBACK_DIR`. Verify the physical paths and nginx document roots and aliases on the NAS. Staging and rollback must be outside every HTTP document root and alias. All three directories must be on the same filesystem and support overwrite by rename. Give the publisher access only to these directories. Keep credential-bearing images out of GitHub Actions artifacts.

## Preflight, publication and device acceptance

1. Dispatch `verify-private-sftp` on the reviewed `main` commit and record the successful run ID. The workflow checks public firmware refusal and uses a pinned-key SFTP session with only `cd` and `pwd` to verify all three canonical paths. NAS-side permissions, mounts and HTTP mappings require the administrative checks above.
2. Dispatch `publish-firmware` once with the full reviewed `expected_commit` and `preflight_run_id`. The workflow checks that the earlier run succeeded in read-only mode on the same commit, then repeats the directory check. A NAS-side directory reserves that commit before publication. Pushes and workflow reruns cannot publish.
3. The publisher verifies the currently served pair, saves verified copies in non-web rollback storage and checks both staged candidate files. It withdraws the old manifest, checks that the authenticated endpoint reports it absent, promotes the image, then verifies the exact image through authenticated HTTPS before exposing the candidate manifest. The final check requires the exact version, size and SHA-256 and denies anonymous and wrong credentials. If promotion fails, the publisher attempts to restore and verify the previously served pair. If verification is unconfirmed, inspect the private copies and live origin before any further action. The device uses two HTTPS URLs, so a request that already fetched the old manifest can overlap this sequence.
4. Before issuing one normal OTA, confirm fresh device voltage and sample validity, relay off, idle state and the normal OTA admission flag. Afterward, record the exact installed version, a new boot and fresh healthy idle telemetry.

## 2026-11-28 TWORIM review

- PR #29 originally pointed to `f8294ae`. Hosted CI run `46117383256` passed at that head. The follow-up uses an isolated worktree and adds one-time dispatch, separate SmartBox SFTP credentials, exact preflight proof and rollback.
- The GitHub environment exists and accepts only the protected branch `main`. No SFTP or Tailscale credentials are configured. NAS and Tailscale administration sign-in is pending. The SmartBox endpoint's trusted key, directory isolation and read-only private SFTP preflight are pending.
- **Publication**: zero runs. Exact new version and SHA-256 over authenticated HTTPS remain pending.
- **Installation**: zero OTA requests. MQTT reported offline and a read-only diagnostic query did not produce fresh device telemetry. HA last logged a disconnect at 14:14:37 KST. Current voltage, relay state, normal OTA admission and installed version remain unconfirmed.
