---
title: SmartBox wiki log
type: log
project: smartbox
status: active
updated: 2026-08-12
source_of_truth: true
---

# Wiki Log

이 파일은 append-only다. 과거 항목을 현재 사실로 해석하지 않는다.

## [2026-08-12] compile | Establish repository-native Obsidian wiki and current-code baseline

- Added the wiki index, current project status, architecture and knowledge-management governance at repository commit `9f2facb`.
- Separated repository implementation, verification evidence and deployed state; classified reports, prompts and completed plan artifacts as historical snapshots.
- Corrected the agent safety contract for Active-Low relays, Main high-impedance cutoff, direction-neutral HIGH/HIGH and full INPUT isolation.
- Recorded current OTA, MQTT, Wi-Fi, telemetry, startup calibration and CI/CD boundaries without claiming physical-device or deployment proof.
- Prepared reusable relay-interlock and pre-OTA hardware-isolation knowledge for provenance-based promotion to the shared Knowledge Hub.

## [2026-08-12] lint | Validate current documentation and record build environment limits

- Verified relative Markdown links for `README.md`, `AGENTS.md`, `HARDWARE_BOM.md`, `SECURITY_CLEANUP_GUIDE.md`, current `docs/*.md` and `wiki/*.md`; historical `prompts/` and `reports/` retain links to files that existed at the time and are excluded from the current-document link Gate.
- Verified every `wiki/*.md` page is routed from `wiki/index.md`, the Knowledge Hub links resolve and `git diff --check` passes.
- Located PlatformIO Core 6.1.19 at `C:\Users\shcat\.platformio\penv\Scripts\platformio.exe`; it is not on the current process PATH.
- `platformio.exe test -e native` did not compile because Windows could not resolve `gcc` or `g++`; no native test case executed, so this is an environment failure rather than a test assertion result.
- `platformio.exe run -e esp32-c6-devkitc-1` produced no captured output and exceeded both 120-second and 300-second command limits; target build completion remains unverified in this turn and should be rerun locally or in CI.

## [2026-10-16] code | Prepare combined MQTT accounts and protected OTA publication

- Owner authorizes SmartBox/Telegraf in Smart Gatekeeper328. New non-retained telemetry and retained632f004 version observed read-only; no actuator command. Existing anonymous credential-bearing firmware HEAD returns200, so fresh credentials require origin protection first.
- Added root-plan/hash-checked nginx origin guard with private backup, whole-tree candidate validation,28 Unix-socket HTTP behavior cases, original-peer CIDR mapping, preserved generated Web Station includes and bounded rollback of new files. Initial NAS invocation stopped before operating writes; confirmed both guard files absent. Config-prefix RED/GREEN and retained diagnostic reporting fix prepared; retry pending.
- Added external HEAD/GET refusal gate before real-credential publication and PR placeholder native/target checks with no deployment. New bounded real MQTT credential preflight prevents empty/placeholder anonymous builds.
- Floating pioarduino HEAD55.3.312 failed: IncompatiblePlatform, Core6.1.19 versus required>=6.2.0. Last successful operating CI31599873352 records55.3.311/dd01e49. Pinned both target envs to full dd01e49; native13 PASS and target build PASS (RAM62156B/flash1342740B), preserving relay/FSM/16MB/OTA behavior.
- Four gate tests and two actual nginx regressions PASS; fixture covers28 HEAD/GET cases. No new credentials, firmware publication/installation, broker activation or physical success claim. Local secrets are placeholder ignored files. New wiki linked and current manuals synchronized; final links/whitespace validation precedes publication.

## [2026-10-16] test | Install OTA origin guard after config-prefix correction

- Owner reran hash-pinned guard05 with interactive sudo. Exact private receipt confirms full native nginx config validation,28 Unix-socket allow/refuse cases, local strict-TLS firmware/version checks and two root-owned include installation. Original generated Web Station files remain intact. Credentials unchanged; external refusal is explicitly not yet verified.
- Candidate CI gains manual verify-origin as a read-only default; privileged publication requires main push or explicit publish-firmware, plus successful native/origin gates. No real firmware/credential publication has occurred during this candidate work.

## [2026-10-16] fix | Separate cold fixture download from nginx startup

- First PR6 hosted native cases passed, then real-nginx fixture reached its5s process-start deadline while Docker was still downloading the pinned image. Added an explicit bounded image pull before fixture startup; no production code/route changes. Rerun the six local origin tests and exact-head hosted checks.

## [2026-10-16] test | Allow read-only external origin probe before merge

- Manual verify-origin may run from the reviewed candidate ref and exposes only the configured URL to anonymous HEAD/GET checks. Privileged firmware build/publication remains main-only and explicit operation gated; PR/default feature probes cannot deploy or read device passwords. This permits independent external refusal evidence before credential rotation or main merge.

## [2026-10-16] compile | Publish coordinated named MQTT provisioning

- NAS isolated16-case authentication/TLS stage now succeeds. Fresh SmartBox IDLE telemetry observed with retained current version0.1.0-g632f004. Preserved current firmware/version privately before publication; firmware SHA256e152da765110050a15e6dca745b8d7e69aa39a59ca4014385a027bd2270a564c.
- Independent external-origin probe38823150167 passes anonymousHEAD/GET refusal. Registered fresh distinct device USER/PASS through GitHub secret stdin without logging values. Existing root-private broker candidate holds the matching password; operating anonymous acceptance remains until installed-device readiness.
- PR6 merged as5e5e334 with existing organization administrator permission because the repository update-lock ruleset rejects ordinary merge. Protection policy was not edited. Main workflow38823428021 started; actual publication/install/health remain pending. Native/target build requirements were passed on exact candidate previously; this turn changes no source/relay/partition behavior.


## [2026-10-16] fix | Prepare pinned private device LAN origin update

- Main5e5e334 firmware publication succeeds and private byte readback matches the staged device credential; original artifact preserved. One OTA request after fresh IDLE did not install within180s. Unknown-command warning confirms reception; installed CA/current URL verify. Owner confirms same network as Target; reported device LAN differs from initial origin allowlist, while Target525 installs/bootsVALID at911.
- Add a root-only pinned existing-guard updater accepting one private/24 or narrower range. Validate the full routing tree with only the geo include substituted, preserve path guard/other ranges, verify actual HTTP fixture and restore exact prior geo/gateway-binding bytes on postcheck failure. Immutable captured source/plan avoids user-writable NAS staging execution.
- Python12 PASS, including real nginx additional-LAN30 HTTP cases, existing28 HTTP cases, original-directory regression, stale binding refusal and exact postcheck rollback. Native/target gates follow. Actual NAS application/device HTTP result remains pending; no broker cutover or actuator command.

- Final local native13 PASS and ESP32-C6 target build PASS after documentation changes; generated git_version bytes restored. Final Python12 PASS after enforcing root-only backup/include parent ownership. Prepared immutable source/plan operator launcher update-ota-network.sh with private failure receipt; no NAS apply or privilege broadening yet.

## [2026-10-16] compile | Merge guarded device-network updater and rollout evidence

- SmartBoxPR8 merged after exact-head hosted native/origin/target CI38826365345 PASS; source-only updater and private immutable operator launcher are ready. Gatekeeper evidencePR440 mergedc8cb880 after trusted-policy PASS. Both repositories have no open PR at this checkpoint.
- Native13, Python12 and ESP32-C6 local gates PASS. Administrator application of update-ota-network.sh remains pending because nginx lies outside the six fixed broker commands. No broker cutover or second SmartBox OTA request before that protected route changes and is verified.


## [2026-10-16] code | Expose bounded last-attempt OTA diagnostics remotely

- Existing OTA failure details are Serial-only, leaving the installed failure stage unobserved. Add advisory phase, boot-local attempt count, manifest HTTP/transport result, raw updater error and queued-request flag to the existing MQTT telemetry object. Do not send URLs, arbitrary error strings or credential data.
- Reset numeric failure codes per started attempt and preserve its phase/result after IDLE recovery. Scheduling/TLS/version checks/relay isolation/watchdog ordering/partitions remain identical. Synchronous OTA still pauses periodic telemetry; these fields are last-attempt evidence, not download progress or durable reboot history.
- Existing native OTA relay-isolation tests and required target build will be rerun; no redundant implementation-mirroring tests added for metadata-only fields. Installed failure/recovery and physical gates remain open. The pending immutable nginx operator launcher is unchanged.

- Local native13, Python12 (including real nginx) and target build PASS. Replaced deprecated volatile increment with equivalent explicit read/add/write and final target build passes without that warning. Generated version-header bytes restored. Actual emitted fields on the installed device remain unverified until protected OTA route application and installation.


## [2026-10-16] code | Merge bounded SmartBox OTA remote diagnostics

- SmartBoxPR9/d5fd84b passes exact-head hosted CI38827586073 (native/origin/target) and mergesb41387e. Main publication38828134589 is queued/live; runtime diagnostic emission is not yet claimed. Existing private-network update launcher source/plan remain unchanged.
- Earlier range-updater main06cd5e0 publication38826769704 completes SUCCESS. Strict-TLS private readback matches version0.1.0-g06cd5e0 and staged MQTT provisioning; that image intentionally lacks the new diagnostic fields. New publication must be read back separately.
- Current verified Backend Target health remains525/boot911/IDLE/OFF with advisory VALID, pending0/gapfalse. The ordinary operator master is absent, so nginx application is not confirmed. No second device OTA, broker cutover, arbitrary shell or new administrator prompt.

- Current diagnostic main publisher38828134589 confirmed live againstb41387e; native/origin steps observed separately from firmware publication/installation. No source-only or CI success is treated as an emitted installed-device diagnostic.


## [2026-10-16] test | Verify diagnostic publication and preserve installed-device boundary

- Main diagnostic publisher38828134589 SUCCESS atb41387e. Strict-TLS private NAS readback0.1.0-gb41387e has matching staged MQTT provisioning and compiled diagnostic fields; SHA256412d0d6d2186f636c14ce99bba37c4d2d1bd34fcf78decfbcc566b2d26e3b9d9. Independent post-publication external HEAD/GET probe38828916009 PASS; its publication job is skipped.
- Fresh subscribe-only observation receives retained advertised0.1.0-g632f004 and nonretained IDLE telemetry without ota fields. New device install/emission remains unproved, and the ordinary operator master is absent. No second OTA, physical command or operating broker change was sent.
- One-time nginx LAN update result is still required before guarded device installation and version-bound broker restaging/cutover. This is an outstanding external dependency, not objective completion; no repeated administrator question or goal pause/completion request.

## [2026-10-16] fix | Measure OTA origin requests after applied LAN correction

- Owner applied the pinned LAN update at01:58UTC; independent receipt guard hashes match. Full config/30 private behavior cases pass; separate outside probe38838309358 succeeds. One post-change OTA request still leaves advertised632f004 with fresh IDLE and no ota object. NAS has two private NIC subnets; actual device HTTP peer/status is not yet observed.
- Add root-authenticated, hash-bound OTA-only request observation with600s activation wait,150..300s active window, private raw log, redacted reports and exact cleanup/watchdog. Optional repair allows one measured403 ESP32 request on one missing narrow private NAS interface LAN; no public/unknown/ambiguous CIDR or persistent SSH/sudo authority. Reboot-safe optional loaders and actual-nginx query/path/status tests included.
- HA outage isolated to stopped existing homeassistant container. Starting it restores external/direct HTTP200 and strict-TLS WebSocket101/auth_required; owner confirms phone access. Automatic restart remains disabled and stop cause is not proven. Newly active anonymous HA MQTT must be included before broker cutover.
- Initial new isolated trace fixture failed on container temp-directory permissions; moved its five cache paths into the test-owned temp directory. Final real-nginx/Python19 PASS before the final report/candidate guard adjustment; rerun plus required native/target builds follow. No trace installed, additional device request, broker cutover or physical acceptance claimed.

- Final local Python19 including five actual-nginx regressions PASS, native13 PASS and ESP32-C6 build PASS (RAM62172B/flash1343626B). Generated version header restored. Captured immutable operator launcher observe-ota-origin.sh prepared privately and syntax/AST checked; no NAS trace activation or additional device command performed.

- Evidence clarification: HA container/HTTP/WebSocket restoration is verified; its prior anonymous MQTT configuration is captured, but a fresh HA-specific MQTT connection is not independently proven. auth_required proves the TLS WebSocket authentication entry, not a completed HA API login. Both remain separate from broker cutover readiness.

## [2026-10-16] compile | Merge bounded origin observer after exact-head validation

- PR10 exact-head7137bf2 hosted38844587014 SUCCESS including native/origin/target; merged7612cd40 with existing administrator merge permission under unchanged update-lock policy. Both repositories openPR0. Local final native13/C6 builds PASS; generated version header restored and changed-document links/whitespace PASS.
- Main publication38844978553 is live; independent outside refusal job passes. Actual new artifact readback is pending. Fresh read-only MQTT still advertises632f004 and sends nonretained telemetry without ota fields. No further OTA or broker cutover.
- Immutable source/plan operator launcher prepared and requested once because six-command broker key intentionally lacks nginx authority. Before any device request, require active trace and actual local200 records for both OTA paths, terminal publication/readback and fresh safe state. Bounded observer is not yet installed; HA remains restored with restart-policy/MQTT-readiness follow-up441.


- Main SmartBox publication38844978553 completesSUCCESS at7612cd40. Independent strict-TLS Tailscale GET readback matches0.1.0-g7612cd4, staged private MQTT provisioning and compiled diagnostic markers; image1397376B/SHA256e47ec307b93030d3b716437ea4d381e06916a48d7983a72b2b4c60be48153a1c preserved privately. Prior published metadata retained before refreshing expected version.
- Post-publication independent external probe38845548162 completesSUCCESS; its build/deploy job is skipped. Both OTA paths remain available through verified Tailscale TLS while anonymous external firmware HEAD/GET is denied. Device installation is still unconfirmed and no new OTA was sent. Temporary operator session is absent; pending bounded nginx trace requires the requested authenticated invocation. HA restart-policy441, fresh HA MQTT/named-account adoption and328 cutover/rollback remain open.

## [2026-10-16] fix | Require measured capture readiness before device OTA

- Owner ran observer01; pinned waiting report and live worker/watchdog verified. Agent activated trace, but initial local HEAD records were absent, so no device request was sent. A later self-check GET/version200 was captured; exact cause of initial missing HEAD records remains unproved. Normal cleanup finished04:09UTC with trace_cleaned=true, no repair/credentials/broker change.
- Require successful loopback HEAD log records for both paths before active/capture_ready. Retry admission is bounded15s, failure reports safe rows/log size/stage and cleans, and watchdog budget includes admission/cleanup. Ordinary sudo cache is unavailable; do not broaden the six-command key.
- Existing Synology403 error_page redirects change uri at log time. Preserve fixed original OTA path labels through immutable-request mapping without serializing queries. Add real nginx HEAD/GET/403-redirect and delayed-admission regressions; loopback/tailnet scopes no longer reported public.
- Python22 PASS before final failure-summary/scope adjustment. Initial test expectations incorrectly assumed no admission retry and immutable fixture query variables; fixed the fixture to use an immutable peer map and count real probe attempts. Final Python/native/target and links/whitespace gates follow. Installed SmartBox failure and broker cutover remain open.

- Final Python22 with real nginx HEAD/error-redirect/admission cases, native13 and ESP32-C6 build PASS (RAM62172B/flash1343626B). Generated version header restored. Actual observer01 cleanup receipt retained; zero device OTA requests. New captured operator session02 is prepared only after source validation; source publication and NAS execution remain separate.

## [2026-10-16] compile | Merge observed-capture gate and prepare session02

- PR11 exact-head35f40e0 hosted38850280716 SUCCESS including native/origin/C6; merged5549f53 under unchanged administrator update-lock policy. Both repositories openPR0. Main publication38850708755 is live; no new artifact/install claim.
- Session01 actual cleanup/one late agent GET retained privately; zero device OTA requests, no LAN repair or broker/credential change. Updated immutable source/plan session02 uses a fresh report key and capture_ready gate; operator authentication requested once because nginx is outside fixed broker authority.
- Local22/13/C6 and links/whitespace PASS; primary wiki/hardware evidence append-only updated. HA HTTP200/Backend ready12 postcheck PASS; HA automatic restart/named-account/fresh MQTT acceptance and328 cutover remain pending.


- Main capture-gate publication38850708755 SUCCESS at5549f53. Strict-TLS Tailscale readback0.1.0-g5549f53 matches staged MQTT provisioning and diagnostic fields;1397376B/SHA256c7a97148c122b293ef2a1e04273732f0b6db925e181f21d6138553ad624ca4e2 preserved privately. Previous7612 publication metadata retained before updating expected version.
- Post-publication outside probe38851138739 SUCCESS with build/deploy skipped. Broker key-only readback remains dedbac762b24a73c91af719eb0a6611f41e177d75b63b20e6949cafe5e216df0 and both anonymous directivestrue. No device OTA or operating credential cutover this turn.
- Current ordinary operator master is absent; updated session02 execution/readback is not confirmed. One fresh authenticated invocation is requested; do not assume execution from elapsed time, bypass sudo/key limits or claim installed-device acceptance. Both repositories openPR0; SmartBox issue7 checkpoint updated in place.

## [2026-10-16] fix | Prepare authenticated WAN OTA after measured external403

- Observer02 actual capture_ready verifies both successful loopback HEAD paths. Exactly one OTA request yields device version200/firmware403 from PUBLIC, followed by fresh IDLE and no installed version. Normal trace cleanup succeeds; no guessed public/private CIDR change or repeated OTA.
- Owner confirms same Gatekeeper router and asks MQTT capability. Command reception is proved by the correlated device download attempt; fresh read-only TLS MQTT confirms632f004/IDLE. No physical lid movement tested. NAS DDNS/egress match each other but differ from the established field peer.
- Add dedicated HTTPS credentials/redirect refusal to existing client, protected production provisioning and a main-only external auth probe. Prepare pinned NAS auth policy/private hashed identity, original proxy preservation/header stripping, real auth fixtures and rollback. Optional legacy public/32 bootstrap is explicit/default-off, artifact/credential bound and server-clock-limited300s, with early closure and later authenticated-check evidence.
- Python38 PASS including actual nginx auth/expiry, query/header privacy, credential-file ownership/symlink checks and activation rollback. Candidate proxy extraction accepts captured NAS source. Original global config has no direct user directive, so verify actual host nginx worker UID/GID instead of assuming a generated include layout.
- No NAS auth install, public exception, broker cutover or extra device command; operator consent/authentication remains required after reviewable source/artifact preparation. Native/C6 final gates follow; relay/FSM/partition/scheduling contracts unchanged.

- Final Python38 PASS includes296 real authenticated-policy HTTP cases across normal/open/expired fixtures, GET/HEAD-only role checks, rollback and input ownership/privacy. Native13 and C6 build PASS (RAM62172B/flash1343884B). Generated version header restored. Fresh distinct256-bit OTA identity stored only in private files and GitHub repository secrets via stdin after auth presence/status checks; no values printed. NAS settings/bootstrap remain unmodified pending approval/authentication.
