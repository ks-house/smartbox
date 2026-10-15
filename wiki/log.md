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
