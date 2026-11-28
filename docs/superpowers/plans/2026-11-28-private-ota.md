# SmartBox private OTA implementation plan

Goal: retire the GitHub publisher's public SFTP dependency without changing device HTTPS downloads, NAS firewall, firmware safety logic or Gatekeeper access.

- Add an owner-scoped, main-only Tailscale identity for the existing SmartBox SFTP service, with a single destination and port, pinned NAS host key and no fallback to public SFTP.
- Add read-only SFTP preflight, then a one-time manual publish tied to a successful read-only run on the same commit. Keep verified NAS-side rollback files, verify the staged image through authenticated HTTPS before publishing the manifest, and verify the final version and digest.
- Use test fixtures without credentials; verify main-only job permissions, invalid host/key/path handling, read-only mode, upload ordering and failures. Run existing source tests and target build.
- Keep live publication gated until the environment is restricted to main, the trusted NAS key is provisioned privately, and an actual private preflight passes. Verify the standard OTA with fresh device version/boot and healthy idle status. No forced/recovery update, relay actuation, or firewall change.
