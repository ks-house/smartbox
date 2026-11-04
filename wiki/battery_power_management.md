# Battery recovery and power monitoring

Status: source implementation and isolated validation; publication/installation are separate gates. Owner approved automatic resume and existing-hardware-first firmware update on2026-10-31.

## Power policy

The system retains its12V lead-acid profile and source0.2.3 default11.0V-inclusive/3s guard (installation pending). Warning=cutoff+0.7V, saving=cutoff+0.4V. Noncritical changes qualify for15s and have0.1V exit hysteresis to avoid repeated radio publications near a threshold. Recovery requires max(12.0V,cutoff+0.7V) continuously for300s with fresh valid samples; a gap>2s, invalid sample or voltage dip restarts qualification. These are software policy defaults, not measured SOC or a battery-manufacturer acceptance claim.

A confirmed critical episode preserves the existing single3800ms fail-open stroke and interlock. Invalid/missing INA219 instead isolates immediately, including failure during that stroke; it never returns synthetic12.2V/0mA. Recovery leaves outputs isolated, clears pending movement/median/debounce and enters idle with3s blind time. Fresh normal detection may then run the existing FSM. Recovery alone does not force a lid stroke or prove physical position.

Protection is persisted. A boot whose NVS says protection or logical lid-open starts isolated recovery rather than a calibration stroke, avoiding repeated movement on power failure or OTA reboot. The explicit lab STARTUP_OPEN regression remains. Old NVS lid state is not actual position evidence.

## Savings and update recovery

Normal/warning/saving distance polls remain50/50/100ms. Voltage/current sample200ms normally or moving,1000ms while isolated. Main yields1ms normally/20ms while isolated; network processing yields100/1000ms. No scheduled night sleep is introduced. After the5min provisioning window, connected Wi-Fi modem sleep is enabled; this preserves the MQTT connection rather than entering deep sleep.

Telemetry periods are30/60/120/300s for normal/warning/saving/protected-or-recovering. Qualified phase changes publish promptly. Authenticated transport, watchdogs, relay100ms+100ms interlock, motor timeout and16MB OTA partition remain.

OTA starts only after30s fresh stable recovery-level voltage and no motor/protection. Admission is checked again before flash. The loop observes power during OTA without actuating; failed/no-update exit cannot clear protection. Existing TLS CA/authentication/redirect refusal and previous bootable-slot behavior are retained. No signature/health rollback feature is claimed beyond existing implementation.

## Telemetry and alerts

Existing smartbox/telemetry and retained smartbox/status carry power_schema1, power_sensor_valid, power_state_code, power_sample_age_ms, power_report_interval_s, power_recovery_remaining_s/count, minimum/recovery voltage and admission flags. Codes: NORMAL0, WARNING1, SAVING2, PROTECTED3, RECOVERING4, SENSOR_FAULT5, UNKNOWN6. No new MQTT principal, topic permission or credential is required. Invalid voltage is null; old raw voltage is not reused as current valid data.

HA discovery adds five power sensors and one measurement-fault binary sensor (26 entities total). The existing voltage entity uses live telemetry and availability from both transport and measurement quality. Motor current remains MOTOR_BRANCH; no total battery Wh, charge current or percentage is inferred before wiring/whole-system measurement.

The NAS observer queries only existing Influx telemetry with the existing collector identity, bounds input, rejects redirects and distinguishes source unavailability, stale telemetry, legacy firmware and power faults. Warning/saving alerts qualify60s; critical faults are prompt and deduplicated. Local owner DSM notifications have a persisted retry queue. OFFLINE means no fresh telemetry beyond2x the announced period+30s, not a confirmed battery/electrical diagnosis. No actuator/control publishing exists in the observer.

[Operator installer](../ops/install_power_monitor.py) verifies the exact captured source, validates the existing query before writing, creates only its dedicated service/config/state and rolls back those new files on failure. It does not restart the broker/API/collector/HA. External test email and account/security changes are excluded. [Grafana dashboard export](../ops/power_dashboard.json) needs an existing authenticated import; a prepared file is not operating dashboard acceptance.

## Validation and field boundaries

Native production-component regressions include permanent shutdown recovery, dip/gap/clock wrap, sensor failure, rejected stale/under-threshold commands, independent timers, no recovery pulse, old relay/stall/OTA contracts and warning chatter. Python checks cover clock/source/quality/legacy-cohort rejection, stale-period handling, deduplication and actual installation rollback under isolated files/service fixtures. C6 compilation uses dummy local provisioning; that image must never be published.

Before operating OTA, preserve the prior artifact/metadata, require reviewed CI publication and authenticated HTTPS content/version readback, observe fresh adequate voltage and send at most one owner-authorized trigger_ota. Installation/new boot/new schema and automatic recovery need actual observation. Physical lid movement and energy savings require separate field evidence; no unattended actuator test is permitted.


## Review corrections before release

Direction output modes are restored only on a subsequent legitimate movement: Main OFF,100ms release, neutral latch preload, OUTPUT restore/direction select,100ms settle, Main ON. Current stall uses magnitude. OTA reserves admission/isolation/state under one controller lock, rejects movement/maintenance while reserved and rechecks current power immediately before flashing. Every non-reboot updater exit retains protection. Missed night checks catch up once that calendar day after adequate power; routine reboot cannot interrupt even the manifest request.

INA219 now uses an independently tested triggered register sampler: verify calibration4096 and the existing0.1-ohm/100uA LSB profile, new conversion-ready, overflow,320mV range, coherent current/shunt and repeated register state. Power-down is explicit between samples. The12V policy rejects readings above16V as unsupported, rather than enabling actuators. These profile assumptions still need physical shunt/load verification. See the [TI INA219 datasheet](https://www.ti.com/lit/ds/symlink/ina219.pdf).

A successful HTTP stream with an Influx error table is a source failure, never device-offline evidence; malformed cohorts and nonfinite/negative sample ages are rejected. Dashboard reporting-period labels describe configuration, and last-receipt age is separate. Final local gates:37 native tests PASS,76 Python tests PASS with9 explicit environment skips, C6 dummy build PASS (RAM62220B/flash1347818B). Source/runtime/physical acceptance remain separate.


## 2026-10-31 operating checkpoint

PR #18 contains the implementation; publication and installed acceptance are still pending. A read-only MQTT observation at 10:10 UTC measured about 12.17 V with a live MCU and legacy BATTERY_LOW_SHUTDOWN. The owner cannot connect an external supply now and will wait for solar charging. Do not weaken recovery/admission thresholds or publish a candidate that the legacy scheduled updater could install before a fresh energy check. No actuator or OTA command was sent.

The existing custom WebStation OTA guard retained a stale Docker bridge address after container restart, causing manifest HTTP 502. The narrow operator in ops/repair_ota_static_origin.py replaces only the two exact OTA content handlers with direct reads of the same static mount, preserves authentication/access/method policy, and removes the Docker-address dependency. It validates the full nginx tree and the existing http worker's metadata/HEAD/range reads in a private Unix-socket fixture. Both prior manifest bytes and image digest are bound to the private rollback artifact; activation rechecks the pair, verifies actual TLS bytes, preserves the existing integrity binding, and restores configuration on failure.

Applied at 10:57 UTC after isolated validation; no new listener, credentials, ACL or container restart. Hosted read-only verification [41194901015](https://github.com/ks-house/smartbox/actions/runs/41194901015) passed authenticated exact prior-image download and anonymous/wrong-credential refusal. Its publication job was skipped. This repairs the server path, not device installation or physical lid operation.

A new Grafana dashboard uses the existing datasource; the old dashboard is preserved. Legacy voltage/current can be graphed, while unreported measurement quality and new policy states must remain unknown. NAS observer installation requires fresh schema 1 and remains pending with firmware installation. General backup issue #461 remains deferred; only the previous OTA artifact pair and changed guard are preserved for this update.


## Explicit initial legacy transition (2026-10-31)

The owner renewed the firmware installation request after the charging hold. Two-minute passive evidence at20:12–20:14 KST measured12.140–12.144V, motor-branch0.4–0.6mA, a continuously increasing uptime and persistent legacy BATTERY_LOW_SHUTDOWN. The initial blanket installation hold is superseded only by this bounded legacy transition; no deployed firmware threshold changes.

The operator [first_battery_transition.py](../ops/first_battery_transition.py) accepts only the captured0.1.0-ge0323a0 legacy, four consecutive live protected observations spanning85–110s, final receipt≤3s, unloaded voltage12.0–16V with≤0.05V spread/no>0.02V fall, motor-branch magnitude≤2mA, continuous uptime, no pending OTA and no new-schema/fabricated fallback frame. The12.0V floor is an operator margin for this first isolated transition, not measured MCU rail health/SOC or a general automatic OTA bypass. Existing e0323a0 saves logical lid-open for protection; reviewed new boot converts it to isolated protection without calibration. Future OTA admission remains fresh stable recovery-level voltage for30s and idle; automatic motor recovery remains12.4V/300s.

The hardware guide routes battery→LM2596→5V MCU separately from motor switching. [TI](https://www.ti.com/product/LM2596) supports regulator input headroom but does not prove installed rail stability or battery capacity. The existing16MB inactive OTA slot and old bootable image remain; [Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/ota.html) documents inactive-slot OTA and redundant otadata. Installed rollback/health evidence is still not claimed.

Before the one deliberate nonretained QoS0 trigger, the caller must verify exact reviewed main CI publication and authenticated HTTPS manifest/image identity using existing credentials. A root-private O_EXCL reservation, file fsync and parent-directory fsync precede publishing; any uncertain outcome retains it and forbids automatic resend. The tool can publish only trigger_ota, never an actuator command. New version/new uptime/fresh valid schema1/protected boot must be observed before acceptance. Preparation, publication and installed acceptance remain separate.

Installed acceptance additionally requires a consecutive post-send valid-schema pair spanning at least30s with increasing uptime consistent with receipt time, and the latest fresh nonretained expected-version online status. Offline, reboot or bad measurement resets acceptance. Lost handoff/acknowledgement is recorded as requested-but-uncertain, and each attempt replaces stale terminal evidence without removing the no-resend reservation. Final local validation:93 Python total/84 executed PASS/9 explicit environment skips, native37 PASS, C6 dummy build PASS, and two exact private-transport failure fixtures PASS.


## Operating incident and independent observer bootstrap

PR18 merged as7a55dd8253fc50681897b0ebe67292499bb10657. Publication41197607762 and exact external authentication/refusal41197969972 pass for0.2.0-g7a55dd8,1401568 bytes, SHA25612c42958bbe9468916af31800fa931bdf7e598315384e66a0deddca4778c1baf. Exactly one nonretained OTA write is confirmed under a durable no-resend intent. The observer did not obtain a new online boot with valid schema1; actual installed version remains unverified. Broker records the old client timeout at20:48:24 KST and later passive observation reports offline. Server/API remain responsive. This is not a successful installation claim, and no physical actuation was requested.

The old/new image headers and two trust anchors match; private MQTT/OTA strings also remain present in both images. Wi-Fi credentials are loaded from NVS through unchanged code. These checks narrow possible causes but do not prove download completion, MCU power, new-application boot or Wi-Fi association. No field subnet route is available from the operator network and the owner cannot inspect LEDs/setup AP now. Preserve both images and require field boot diagnostics; no blind resend/reboot/rollback.

The NAS observer may now be installed explicitly with --bootstrap when the Influx query is healthy but the device is legacy/offline. It still rejects malformed/unavailable sources and the ordinary installation mode still requires schema1. Captured legacy0.1 publishes every30s, so a legacy receipt older than90s is OFFLINE rather than permanently LEGACY_FIRMWARE. No stale voltage is presented as current valid measurement. The read-only service automatically recognizes schema1 when it arrives and publishes no MQTT/control commands.


## Later installed evidence and remaining power fault

At21:55KST retained status and HA battery discovery explicitly report0.2.0-g7a55dd8. Fresh schema1 records are present, with sensor_valid0/state5 and no valid battery voltage.21:49/21:54/21:59KST uptime600969→900982→1200993ms corroborates the same new boot across10min. This supersedes the earlier unknown/offline interpretation; it confirms installed execution but not healthy measurement or automatic actuator recovery. No additional OTA/reboot was sent. The candidate manifest was reversibly unpublished; both binary artifacts and original metadata remain private.

NAS installation exposed systemd219, which rejects --now. The installer separates enable/start while preserving rollback. The failed attempt created only an empty program directory and two root-private state markers; an exact owner/mode/hash/absent-service check permits preserving that failed state by rename and removing only the empty created directory before retry. No collector, broker, HA or API restart.

Observer acceptance at22:13:20KST confirms active service, pinned program98102212, fresh typed POWER_SENSOR_FAULT/no voltage claim, repeated60s observations, exactly one accepted local-owner event and empty retry queue. Collector bytes, held candidate image and API readiness are unchanged. Source98 Python total/89 executed PASS/9 explicit skips, native37 and C6 dummy build PASS. Dashboard receipt age now selects the latest voltage OR schema receipt; the real Influx query returns133.7s even while voltage remains null. This prevents measurement invalidity from being confused with no telemetry.


## 0.2.1 correction candidate, not deployed

User requests firmware-first remote investigation. See [INA219 regression](ina219_regression.md): restore the known continuous Adafruit read path with success/profile/calibration/range checks and numeric failure diagnostics; no synthetic voltage. A future explicit authenticated-controller maintenance request may recover from sensor-fault protection without releasing motors, while ordinary/automatic OTA keeps its existing gate. Unknown battery power is not certified by this option. Current0.2.0 cannot use the new option and no compatible live admission has been proved; release hold remains.

### 2026-11-02 field observation

After owner reinstallation the corrected continuous reader reports5 consecutive valid/fault0 frames. Latest collector receipt19:29:49KST:12.00006V, protected3/actuation0, threshold12.4V and300s dwell, recovery_count0; since-boot minimum11.78005V. Voltage/current are no longer null. Five-minute reporting and same-boot uptime progression establish current telemetry, while solar charging current/SOC/DMM accuracy and physical motor success are not measured by these frames. Leave thresholds and outputs unchanged; accept recovery only after the actual above-threshold dwell and observed state/count transition.


## 2026-11-03 요청: 충전용 닫기 전용 복구

소유자는 뚜껑을 닫아야 충전된다고 보고했고, 보호 전체 OFF 대신 닫기 전용 복구를 선택했다. [설계안](../docs/superpowers/specs/2026-11-03-low-power-close-design.md)은 사용자 승인 후0.2.2 후보 소스에 구현했다. 보호 래치를 유지하는1회 닫기, 강제 OTA, 회복 전 자동 열기 금지와12.0V/300초 후 새 감지 재개가 대상이며 현장 설치·물리 닫힘은 미확인이다. 기존 0.2.1은 보호 중 force_close와 유효 저전압 OTA를 허용하지 않는다.


### 2026-11-03 implementation checkpoint

Approved source now lowers release to max(12.0V,cutoff+0.7V) for300s and implements an isolated close-only Controller path, one attempt per boot. Existing cutoff11.3V remains; invalid/below-cutoff power blocks admission and stops motion. Maximum3800ms, signed-current magnitude guard and relay interlock remain. Terminal stroke timing never confirms actual physical lid position. Deployed0.2.1 retains old12.4V gate until updated; this paragraph is the historical Task1 checkpoint; command/updater integration is implemented in the subsequent0.2.2 candidate below.


## 2026-11-03 후보0.2.2: 충전용 닫기와 강제 OTA

이 기능은0.2.2 후보 소스에 추가됐다. 현장0.2.1-g4a57756에는 아직 없으며 빌드/버튼 정의는 현장 설치 증거가 아니다.

- 0.2.3 후보는 보호 진입 기본11.0V 이하/3초로 변경한다. 보호 해제 기준은 기본12.0V/300초(설정 cutoff가 높으면 max(12.0,cutoff+0.7))로 낮춘다. 조건 충족 전에는 사람 감지/일반 열기가 동작하지 않는다. 해제 시 자동 스트로크 없이 이후 새 사람 감지부터 재개한다.
- HA의 **충전을 위해 닫기**는 보호 중 유효 전압이 기본11.0V 초과일 때만 닫기1회를 요청한다. 최대3.8초이고 저전압·센서 오류·과전류는 중단한다. 완료 뒤 보호를 유지하고 출력은 격리한다. 시간 종료 표시는 실제 닫힌 위치를 보증하지 않는다.
- **강제 펌웨어 업데이트**는 기본11.7V 이상30초·부팅120초·모터 정지 조건으로 일반 보호 대기/동일 버전 생략을 우회한다. 실제 전원 안정성을 보증하는 조건은 아니다. 센서 오류는 별도 기존 복구 경로이며, TLS/인증 실패와 게시 보류404를 무시하지 않는다.
- 두 버튼은 기존 MQTT 권한 안에서 HA command_template으로 매번 새 요청 ID와60초 기한을 만든다([HA 공식 문서](https://www.home-assistant.io/integrations/button.mqtt/)). retained/중복/만료/재사용을 거절하고 기한 watermark를 NVS에 먼저 저장한다. 시계 미동기화/저장 실패도 거절한다. 같은 초의 연속 클릭은 기한 재사용으로 거절될 수 있다.
- 닫기 시도는 부팅당1회, 강제/센서 복구 OTA 예약도 합쳐 부팅당1회다. 유효 요청 기록은 RAM8개 제한으로 가득 차면 거절하며 자동 반복하지 않는다. 새 MQTT nonce만 바꾸어 이 제한을 우회할 수 없다. 구동 실패 후 임의 재부팅/자동 재시도를 하지 말고 원인을 확인한다.
- OTA는 기존 비활성 슬롯에 쓰고 끝의 이미지 검증 후에만 boot partition을 바꾸는 SDK Update 경로를 유지한다. 동기 HTTP flash 진행 중 소프트웨어 즉시 취소·새 이미지 boot-health 자동 rollback은 보장하지 않는다. 다운로드와 설치/재부팅 건강은 구분한다.
- 새 데이터: power_charging_close_state(0없음/1실행/2시간종료/3저전압/4센서/5과전류/6기한만료/7중단), power_charging_close_used, power_force_ota_allowed, power_force_ota_voltage_v. 기존 schema1 필드는 유지한다.

현재0.2.1은 유효한 저전압 보호 중 force_close 및 OTA를 거절한다. 새 기능을 소급 등록하는 원격 설정은 없다. 최초 적용은 USB 또는 기존 설치 조건 확보가 필요하며 운영 manifest hold는 별도 승인/검증 전 유지한다.


## 2026-11-03 USB adoption accepted

Same physical SmartBox receives exact0.2.2-gdb96897 via guarded USB write/readback with original boot/table/NVS prefix preserved during programming. NativeUSB actual firmware line and30s continued execution accepted. Owner confirms bare board/no INA219, so fault10 is not connected-field measurement failure evidence. Connected voltage/physical charging close/new HA buttons/recovery/network/OTA health remain field Gates; see [bench evidence](bench_usb_acceptance.md).


## 2026-11-04 승인된11.0V 이하 기준

[새 기준 및 저장값 전환](low_voltage_threshold_20261104.md): 이전 기본11.3V 저장값까지11.0V로 전환한다. 앞선 날짜별11.3V 기록은 당시 정책이다. 새 source0.2.3을 현장 적용/확인하기 전에는 설치0.2.2 기준을 변경했다고 주장하지 않는다.


## 2026-11-04 배터리 경고 조건 변경

0.2.4 후보는 배터리 경고/알림을 유효11.0V 이하 연속300초일 때만 적용한다. 전압 회복·무효 표본·2초 초과 측정 공백은 다시 시작한다. 내부 전원 모드 WARNING/절전·보호 상태와 owner 경고를 구분한다. 모터 차단·센서 오류 즉시 보호 및 기존12.0V/300초 해제 조건은 유지한다. [경고 계약과 운영 수용](battery_warning_20261104.md)을 따른다. 실제 배포 결과는 해당 문서의 후속 기록으로 구분한다.


## 2026-11-04 순간 전원 측정 재확인

0.2.5 후보는 순간 범위/센서 오류를 배터리 부족5분 hold와 분리한다. 무효 측정 순간 출력은 즉시 격리하고 SENSOR_RECHECK에서 정상 표본1초 확인 후 대기로 복귀한다. 중단된 동작은 자동 재시작하지 않으며 자동 감지는 부재10초 후 새 접근으로 재무장한다. 실제11V/3초 보호, 경고11V/300초,12V/300초 회복과 구동 안전 조건은 유지한다. 명시 닫기는 논리 IDLE에서도 허용한다. [사건/계약/검증](sensor_recheck_20261104.md)을 따른다.
