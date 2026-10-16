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

## 2026-10-16 applied route correction and bounded origin trace

The owner applied the device-LAN update at01:58UTC. Independent receipt readback
matches both pinned guard hashes; the full nginx configuration and30 private
HTTP cases pass. Independent external probe38838309358 passes after the change.
Its receipt's external_refusal_verified=false is an explicit local-tool scope
boundary, superseded only by that separate external probe.

One further OTA request after this verified change still produced only the old
advertised0.1.0-g632f004 and fresh IDLE telemetry, without the new ota object.
The observed NAS interfaces have two private LANs; reported device LAN alone
does not establish the HTTP peer seen by nginx. Do not guess another allowed
range or repeat OTA solely because the observation window expired.

ops/observe_ota_origin.py adds an operator-authenticated bounded trace for only
/version.json and /firmware.bin. It verifies the existing routing/guard/gateway
hash binding and the whole candidate nginx tree, then waits up to600s for an
owner-readable activation flag. The active observation lasts150..300s. Root-only
logs omit query strings, authorization and forwarded headers; owner reports
contain status, URI, ESP32-agent boolean and a network scope, without peer
addresses or arbitrary agent strings. It sends no device or actuator command.

Optional --repair-private-nas-lan permits one correction only after a real403
binary request from an ESP32 agent on exactly one missing RFC1918 NAS interface
LAN with a /24 or narrower mask. Public, unknown, broad and ambiguous ranges
are rejected. The existing updater performs full nginx/HTTP/rollback checks and
preserves other routes and the broker binding. This option is explicit, not the
default. User-agent matching supplements the measured socket peer; it is not
authentication. Installed-version acceptance and outside refusal remain separate.

Trace includes use volatile runtime files behind optional wildcard loaders.
Normal/error cleanup removes only exact captured bytes and reloads; a detached
watchdog retries cleanup after the maximum session lifetime. Missing volatile
files after reboot leave harmless loaders. Concurrent changes are preserved
rather than deleted. A root-private raw log is retained as incident evidence.
The six fixed SSH broker commands are unchanged; this tool needs a one-time
operator-authenticated invocation and does not grant ongoing nginx privileges.

Home Assistant was independently found stopped, and starting its existing
container restored direct/external HTTP and TLS-verified WebSocket auth_required entry.
The owner confirmed phone access. Automatic restart is still disabled; exact
stop cause and recurrence prevention remain open. HA is running again with previously captured anonymous MQTT configuration;
verify its actual current MQTT connection and adopt its named account before
broker authentication cutover.

## Capture admission and Synology error redirects

The first operator trace activated at04:04UTC and cleaned itself at04:09UTC.
Its initial two local HEAD probes did not appear in the report, while a later
agent GET of /version.json was recorded200. No device OTA was sent and no LAN
repair occurred. Actual capture works, but the initial missing records do not
prove whether reload timing, request handling or another transient caused it.

The observer now enters verifying_capture after reload and retries strict-TLS
local HEADs within a bounded15s admission window. It announces active only after
the root-private log contains successful loopback HEADs for both paths; the
report exposes capture_ready and verified-path evidence. Failure preserves
redacted captured rows, log size and failed stage, then cleans exact trace files.
The watchdog budget includes that admission and cleanup time.

Synology's existing403 error_page redirects can change $uri before access
logging. A map of immutable request URI to fixed /version.json and /firmware.bin
labels preserves the original OTA path through those redirects; the raw request
URI/query string is never serialized. The real-nginx regression includes HEAD,
GET,403 internal redirects, spoofed forwarded headers and query-token omission.
Reports now distinguish LOOPBACK/TAILNET from public peers. No routing, credential,
TLS, relay or persistent SSH permission contract is changed.

## Measured remote-site failure and authenticated HTTPS candidate

Observer02 established both real loopback HEAD records before active. One
device OTA request produced an ESP32 version GET200 and firmware GET403 from
outside the allowed private ranges. Fresh post-attempt IDLE continued; no new
version/reboot was observed. The trace finished and cleaned normally. NAS DDNS
and egress agree, while the one global MQTT field peer differs from both.
The owner confirms SmartBox shares Gatekeeper's router. This is a download
origin-policy failure, not evidence of MQTT command-delivery failure or a sensor
problem. Read-only MQTT recheck confirms old632f004, connected/fresh IDLE.

The prepared client adds a distinct HTTPS download identity to both existing
TLS requests and disables redirects. Credentials remain in the private build
header and HTTP Authorization, never URLs. Production publishing requires
non-placeholder256-bit credentials and same-origin fixed HTTPS paths. The
main-only authenticated external probe verifies anonymous/wrong credentials
denied, valid credentials accepted, TLS chain/hostname and optional image hash.

The candidate NAS policy protects exactly /version.json and /firmware.bin,
preserves the original proxy body and private/tailnet routes, and strips OTA
Authorization before the upstream. Only a salted password hash is group-readable
by the verified nginx worker; private input is owner0600 data pinned by digest,
not executed code. Full original-prefix config validation, real nginx auth
fixtures, two-file/gateway binding and exact rollback precede live acceptance.
Unconfirmed rollback preserves the hash file for recovery.

A legacy device has no download credentials. The prepared optional bootstrap
requires explicit approval, one exact observed global IPv4 /32 refusal bound to
the reviewed field connection, and a still-connected MQTT peer. It is disabled
by default. The root worker opens it only after the protected image version/hash,
new credential bytes and client schema match. A server-clock map rejects new
unauthenticated requests after at most300s even if controller cleanup fails.
First complete ESP32 firmware transfer closes the peer exception early.
Only a subsequent authenticated version check with the required realm counts
as remote authentication acceptance; download itself is not installation.

All devices sharing that source public IP could download the credential-bearing
image during an authorized bootstrap window. A private onsite/Tailscale delivery
route avoids that exception. No public exception, NAS auth policy or credential
cutover has been applied by this candidate work. Actual install/reboot/fresh
health, authenticated remote check, external refusal and HA/client readiness
remain required before broker activation.
