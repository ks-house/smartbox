# Combined MQTT migration and private OTA origin

SmartBox shares its NAS broker with Smart Gatekeeper. The owner authorizes moving
both systems and Telegraf to distinct named principals under the related
[broker migration issue](https://github.com/ks-house/smart-gatekeeper/issues/328).

## Evidence and activation boundary

Current source632f004 sends MQTTS credentials when configured and consumes
smartbox/command. It publishes two telemetry, four event, status/config/log topics
and20 HA discovery configs. The active telemetry collector was configured without
authentication on the broker's plaintext listener. A bounded subscribe-only
observation receives new SmartBox telemetry; retained version matches632f004.
Neither retained status nor telemetry is signed physical actuator proof.

Anonymous strict-TLS HEAD currently obtains the credential-bearing firmware file.
New device credentials must not be published until the origin is protected.
No new password has been published by this candidate.

## Origin guard and recovery

ops/install_ota_origin_guard.py accepts a private non-secret plan. It verifies
root-controlled routing source hashes and the actual Web Station HTTP/user include
hooks, snapshots original files into root-private storage and validates the full
current nginx tree with candidate includes. It does not overwrite generated Web
Station routing. A private Unix-socket nginx fixture checks28 allow/refuse cases,
including both methods, encoded/normalized paths, IPv4/IPv6, forwarded-header
spoofing and unrelated mobile/Target routes.

Default mode validates only. Explicit --apply creates two new root0600 include
files. The guard evaluates the original socket peer and denies external access
only to /firmware.bin; configured LAN/Tailscale/loopback ranges keep the installed
HTTPS updater route. Source/byte checks prevent silent drift or overwrite.
Failed config/reload/local strict-TLS route checks remove exactly the new files
and reload the preserved configuration. This preserves the existing updater;
it does not add signed firmware, encrypted content or device rollback support.

The initial NAS attempt stopped before activation because a copied main config
changed nginx's relative-include directory. Both new operating include files were
confirmed absent. An actual isolated nginx regression reproduces that failure
and passes with an unreferenced root0600 validation file in the original main
config directory. The temporary file is removed on either result. Failed command
stderr is now saved in an owner-readable private error report.

[NGINX geo documentation](https://nginx.org/en/docs/http/ngx_http_geo_module.html)
and [original-peer variable](https://nginx.org/en/docs/http/ngx_http_realip_module.html)
describe the address selection. Forwarded headers never define the production
allowlist source. An external check is still required because upstream routing
can alter observed peer addresses.

## Publication gate

A main-push or read-only manual CI job tests anonymous HEAD and range GET from outside the home LAN,
including spoofed private forwarded headers. Only401/403 count as denial;
redirects, successful downloads,404, transport failures and5xx never pass.
The privileged build/deployment job depends on that successful gate. PR jobs use
placeholder credentials and cannot publish. Device local OTA/recovery must be
verified separately before treating the broker migration as complete.

Real MQTT username/password must be non-placeholder and fit the new bounded
credential format; empty credential builds fail rather than reverting to
anonymous provisioning. Do not print/store real values in the private inventory,
workflow logs or tickets. Broker passwords and actual installed-device agreement
remain a separate migration step.

## Reproducible build and current validation

The floating pioarduino branch now requires a newer PlatformIO Core and failed
with Core6.1.19. Both ESP32 environments are pinned to
dd01e49e4695933bc29c1bf6be9a19a6572527a6, the platform commit recorded in the last
successful operating build, retaining Arduino3.3.11 and16MB OTA partitions.

- Native13 safety/FSM cases PASS, including relay interlock and OTA isolation.
- ESP32-C6 placeholder target build PASS after pin: RAM62156B/flash1342740B.
- Origin gate/unit and actual nginx integration6 tests PASS; the source installer
  fixture executes28 HTTP policy cases and the config-prefix RED/GREEN regression.
- NAS retry: exact receipt confirms full config valid/28 cases/local strict-TLS paths and guard installation. External refusal and actual device download remain pending.
- No new credentials, device firmware, actuator command or broker cutover yet.


## 2026-10-16 coordinated rollout and device-network correction

PR6 is merged as5e5e334 and main publication38823428021 succeeded.
Independent external origin probe38823150167 passed. Private artifact readback
matches the new device principal/password, while the previous firmware/version
are preserved privately. One OTA request after fresh IDLE telemetry did not yield
a new-version reconnect within180s. A non-effecting unknown-command probe received
the expected device warning, confirming command reception. The installed CA
verifies current OTA HTTPS/MQTT TLS and the compiled version URL serves the new
manifest; the actual device HTTP failure remains unobserved.

The owner confirms SmartBox uses the same network as Smart Gatekeeper. Its
reported LAN differs from the original NAS-side allowlist. The matching Target
successfully installed signed/encrypted2.1.525 and reported boot911/VALID/IDLE/OFF.
This is a lead for the SmartBox-only private firmware path, not proof of a
specific rejection response.

ops/update_ota_origin_guard.py prepares a controlled extension of the installed
guard. It accepts exactly one additional RFC1918 IPv4 /24 or narrower range,
pins both installed guard hashes and the broker gateway's origin binding, and
preserves the path guard, existing ranges and every unrelated routing include.
It validates the complete nginx tree from the original config directory and
runs isolated actual HTTP cases before activation. Failed post-check restores
the exact old geo/config bytes and reloads once. Successful activation updates
only the geo hash in the restricted gateway's origin binding; no broker/client
credential or account change occurs. Administrator code is delivered as captured
immutable interpreter arguments, not executed from a mutable NAS staging file.

Actual NAS range update, external refusal recheck and installed SmartBox health
remain pending. The six fixed broker maintenance commands intentionally do not
grant nginx configuration authority. This one-time operator-authenticated range
correction does not broaden SSH/sudo privileges. Broker cutover stays blocked
until this device is prepared. Track the operating gap in GitHub issue7.


## Remote OTA diagnostics (source preparation)

smartbox/telemetry now adds a bounded ota object; existing fields remain intact.

| Field | Meaning |
|---|---|
| state | Existing OTA state such as CHECKING, UP_TO_DATE, FAILED or SUCCESS |
| phase | Last started attempt phase: IDLE, VERSION, FIRMWARE or REBOOT_PENDING |
| attempts | Attempts started since this boot; reset by init/reboot |
| manifest_http_code | Version-manifest HTTPClient result, including negative transport codes |
| updater_error | HTTPUpdate raw error for a failed binary update; not an HTTP status |
| request_pending | A subsequent forced check is queued; last-attempt codes remain separate |

Each started attempt clears the prior HTTP/updater codes before recording new
results. Failed phase/results remain available after the FSM returns to IDLE.
No URL, raw error text, firmware contents or credentials are sent in these fields.
OTA scheduling, TLS verification, version comparison, relay isolation, watchdog
ordering and partitions are preserved.

The NetworkTask publishes telemetry after synchronous OTA processing returns.
This is last-attempt advisory state, not continuous download progress or
persistent crash/power-loss history. A reboot resets it; installation still
requires new-version reconnect and fresh idle evidence. Installed-device error
and recovery verification remains open under issue7.
