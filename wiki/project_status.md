---
title: SmartBox current project status
type: reference
project: smartbox
status: active
updated: 2026-10-31
source_of_truth: true
applies_to:
  - firmware
  - hardware
  - mqtt
  - ota
---

# 현재 프로젝트 상태

> MCU publication baseline: `7a55dd8253fc50681897b0ebe67292499bb10657` (0.2.0-g7a55dd8; installed acceptance unconfirmed)
>
> 이 문서는 checkout에서 확인한 구현을 설명한다. 현장 설치 또는 실기기 합격을 자동으로 뜻하지 않는다.

## 2026-10-31 운영 경계

- 소스/CI/운영 게시와 외부 검증 통과 후 OTA 쓰기1회. 초기10분 창에서는 확인되지 않았지만, 이후 retained 상태·HA discovery에서 실제0.2.0-g7a55dd8 설치와 부팅을 확인했다. 신규 schema1 자료도 지속 수신된다.
- 현재 전원 측정은 sensor_valid=0/state5(SENSOR_FAULT)이며 전압·전류는 무효다.21:49/21:54/21:59KST uptime600969→900982→1200993ms로 같은 새 부팅의 지속 실행은 확인했다. 자동 모터 재개/실물 동작은 미확인이고 측정 무효 원인(I2C/설정/하드웨어)은 아직 확정하지 못했다. 현장 로그/원시 레지스터 확인이 필요하며 소유자는 지금 확인할 수 없다.
- 브로커/API는 응답하고 구·신 이미지 헤더/인증서/접속 식별값 대조는 일치했다. 이전 이미지를 보존하며 추가 OTA/재부팅/구동/추측성 rollback을 하지 않는다.
- 버전 manifest만 가역적으로 게시 보류하여 자동 재-upgrade/downgrade를 막고 두 이미지를 비공개로 보존했다. 독립 NAS 감시는 systemd219 호환 수정 후 실제 활성화됐다.22:13KST 반복 관측과 측정 오류 알림 수락/중복0을 확인했고 수집기·후보 이미지·API는 유지됐다. 전압이 null이어도 schema1 수신으로 자료 신선도를 표시한다.

## 한눈에 보기

| 영역 | 저장소 최신 구현 | 검증 경계 |
|---|---|---|
| Controller | 11-state FSM, 5-sample median, debounce, post-close blind time, deadlock self-healing | native test 대상이나 실제 센서·기구 반복 시험은 별도 |
| Actuator | Active-Low relay, Main high-impedance cutoff, 100 ms 방향 전환 interlock, 3.8 s 기본 timeout | GPIO/접점/부하/과전류 실측 필요 |
| Startup | NVS의 logical lid-open/protection이면 출력 격리 후 전압 회복을 확인하여 자동 재개 | NVS는 실제 위치 증거가 아니며 boot 자체로 구동하지 않음 |
| Wi-Fi | 부팅 후 5분 AP+STA captive portal, 이후 STA, 15초 연결 재시도 | AP/OEM/router 현장 복구 증거 별도 |
| MQTT | PsychicMqttClient, MQTTS URI, Root CA, command/config, LWT, HA discovery 26개 | broker 수락과 물리 동작 완료는 별개 |
| Telemetry | 전원 schema1, 유효성·보호·복구 상태, MQTT 30/60/120/300초 적응 보고 | timestamp는 `millis()`이며 wall-clock epoch가 아님 |
| OTA | 부팅/일정 HTTPS 확인, 전력 조건과 원자적 격리 예약, 실패 시 보호 유지 | signature, rollback, post-reboot health confirmation은 확인되지 않음 |
| CI/CD | native test → target build → `firmware.bin`/`version.json` SFTP | main push가 배포를 발생시키므로 운영 승인 경계 필요 |

## 현재 FSM

```text
IDLE
  → OPEN_START
  → OPENING
  → HOLD
  → CLOSE_START
  → CLOSING
  → IDLE
```

보조 상태는 `EMERGENCY_STOP`, `BATTERY_LOW_SHUTDOWN`, `STARTUP_OPEN`, `OTA_UPDATING`, `MAINTENANCE`다.

- 열림 중 stall 3회는 물리 open limit로 취급해 relay를 끄고 HOLD로 이동한다.
- 닫힘 중 stall 3회는 `EMERGENCY_STOP`으로 이동한다.
- 저전압이 기본 11.3 V 미만으로 3초 지속되면 남은 전력으로 open 방향을 구동한 뒤 timeout 후 relay를 격리한다.
- `STARTUP_OPEN`은 NVS가 이전 lid 상태를 open으로 기록했을 때만 `main.cpp`에서 초기 상태로 선택된다.

## 코드에서 확인한 기본값

| 설정 | 기본값 |
|---|---:|
| 감지 거리 | 50 cm |
| debounce | 선언 300 ms, NVS load 후 최소 1000 ms로 보정 |
| actuator time | 3800 ms |
| open hold | 10000 ms |
| cooldown | 3000 ms |
| maximum hold | 180000 ms |
| stall current | 3000 mA |
| emergency recovery | 5000 ms |
| OTA hour | 00:00 |
| heartbeat transmit interval | 10 min |

## 문서 감사에서 바로잡은 항목

- 고정 04:00 재부팅 표현을 제거했다. 실제 재부팅 시각은 `(otaHour + 1) % 24`다.
- OTA의 “1시간 주기” 표현을 부팅 1회 + 설정 시각 일 1회로 고쳤다.
- Home Assistant discovery 수를 코드 배열 기준 20개(7 buttons, 6 sensors, 2 binary sensors, 5 selects)로 고쳤다.
- MQTT 라이브러리를 초기 제안의 AsyncMqttClient가 아니라 현재 `PsychicMqttClient`로 정리했다.
- Active-Low relay 안전 상태를 GPIO LOW 기본값이 아닌 HIGH neutral/Main INPUT 및 완전 INPUT 격리로 정리했다.
- 과거 telemetry 계획 문서의 InfluxDB 직접 전송과 absolute `file:///` 링크를 현행 문서에서 분리했다.

## 남은 검증 Gate

1. Windows에 `gcc/g++`를 설치하거나 PATH를 연결해 native tests를 재실행하고, 5분 local timeout을 넘긴 target build는 CI 또는 장기 실행 terminal에서 완료 여부를 확인한다.
2. 실제 relay module polarity, boot glitch, 100 ms contact separation, dual actuator current를 계측한다.
3. ECHO 5 V 출력 가능성을 확인하고 3.3 V 입력 보호를 실물 배선에서 검증한다.
4. OTA artifact identity, 실패 rollback, reboot 후 version/health 확인 절차를 추가한다.
5. 원격 `force_open`, `force_close`, `reset_emergency`, `trigger_ota`의 인증·권한 경계를 운영 환경에서 확인한다.
6. main push 자동 SFTP 배포의 승인·복구·보존 정책을 문서화한다.

## 2026-10-16 combined broker migration candidate

- Owner includes SmartBox/Telegraf in the shared broker authentication transition. Current device telemetry is observed; named client compatibility and broker cutover remain pending.
- Candidate adds a private OTA origin guard and external anonymous HEAD/GET refusal gate before credential-bearing publication. An initial NAS pre-validation failed before activation; absence of both guard files verified. Relative-config-prefix regression is reproduced/fixed; native NAS retry remains pending.
- Floating platform HEAD failed with Core6.1.19; pin to last-successful operating platform dd01e49 restores target build. Native13, placeholder C6 build and six origin tests with actual nginx pass.
- No new operating credentials, firmware/actuator commands or broker activation. See [migration](mqtt_broker_migration.md).


## 2026-10-17 shared NAS transition coordination

WSL source main is verified ate0323a0. SmartBox shares Mosquitto with Smart Gatekeeper, HA and Telegraf; the operating broker still permits anonymous clients until the HA preparation/log acceptance gate passes. Existing private schema2 candidate preserves Backend credentials and provides distinct Target, SmartBox, HA, telemetry and diagnostic identities.

The agent is preparing a separately signed root maintenance path to diagnose/update the operator without repeated administrator entry. Registration itself does not change a device account, broker policy, firmware, relay/FSM or OTA route. Source, staged candidate, actual device version, authenticated reconnect and physical lid movement must remain separate evidence. No actuator/OTA command is issued by this coordination checkpoint.


## 2026-10-17 accepted authenticated shared broker

Final joint acceptancec1c77a8f at02:56UTC confirms installed SmartBox0.1.0-ge0323a0
fresh telemetry after actual broker replacement, both anonymous=false listeners,
separate named device/collector and collector RestartCount0. HA retains its scoped
SmartBox control authority; Target namespaces remain isolated. Eight services run;
original broker/collector are stopped/no-autostart and preserved for rollback.

Read-only Influx query200/latest02:53:58UTC confirms stored data after same-image/
persistent-data-preserving DNS recovery. Grafana's exact smartbox_influxdb name
resolves and actual Influx mapped4086/health passes. Authenticated HTTPS OTA and
installed-version acceptance preceded cutover; no additional device OTA/reboot/
actuator command or physical lid verification is performed here.

Signed root/fixed-role maintenance now works without owner password handoffs.
Additional NAS settings permission is only2026-10-17. Gatekeeper issues441
original stop-cause/alarms and447 event JSON remain follow-ups.

[Cross-project accepted evidence](https://github.com/ks-house/smart-gatekeeper/blob/main/wiki/broker_cutover_2026_10_17.md).
This acceptance supersedes earlier pending preparation checkpoints below.


## SmartBox7 remaining diagnostic gate

A manifest-only, peer/account/time-bound failure helper and real-nginx52-case fixture are prepared. No new firmware publication is required; actual FAILED404 then UP_TO_DATE200 device telemetry and cleanup remain pending. See [acceptance](ota_diagnostic_acceptance.md).


## 2026-10-17 installed diagnostic gate accepted

Signed operating test357864821 succeeds on installed0.1.0-ge0323a0 and protected
artifact333e23df00876b5464689a9a7dda93b0492989e4055c97c9814878a50d200fd3.
Fresh nonretained telemetry proves attempts2→3→4 with UP_TO_DATE200→FAILED404→
UP_TO_DATE200, all VERSION/IDLE, updater_error0/request_pendingfalse and increasing
millis32421139→32451213→32511362. Actual HTTP peer is measured before admission;
broker-visible private addresses are not substituted. One prior same-version
admission check is separate from the two test attempts.

Full nginx validates the narrow account/peer/version-path rule, server time caps
it at120s and an independent watchdog is armed. Both runtime files are removed and
the original guard hashes preserved; API64b5c2f readiness passes. No public download
exception, credential change, actuator command or firmware phase/install.

PR15/source9e46d4e passes39010352192 Python/native/C6 and merges2c148e19. The redundant
main39011920146 is cancelled. No helper/documentation firmware publication is needed.
[Joint operating evidence](https://github.com/ks-house/smart-gatekeeper/blob/main/wiki/followup_acceptance_2026_10_17.md).
Physical lid/electrical/rollback qualification remains distinct.


## 2026-10-31 automatic battery recovery candidate

[Battery power management](battery_power_management.md) describes the owner-approved automatic recovery, profile validity, savings, OTA reservation and monitoring. Source verification passes37 native/76 Python (9 environment skips)/C6 dummy compilation. Reviewed production publication, installed recovery and monitoring acceptance remain separate; no local dummy image is eligible for deployment.


## 회귀 수정 후보0.2.1 (미적용)

- 업데이트 직전 정상 값과 이후 전부 무효인 이력을 근거로 커스텀 INA219 경로를 우선 회귀 지점으로 다룬다. 현장 센서 고장으로 단정하지 않는다.
- 기존 연속 측정 API 복귀, 읽기 성공/프로파일/보정/범위 검증과 단계별 진단을 추가했다.
- 현재0.2.0의 센서 오류가 강제/자동 OTA를 함께 차단하는 복구 경로 결함도 확인했다. 후보는 명시적·만료·단발·출력 격리 유지 복구 경로를 분리하지만 현재 바이너리에 소급 적용할 수 없다.
- [수정 후보와 증거 경계](ina219_regression.md)를 따른다. 실제 장치는 여전히0.2.0/SENSOR_FAULT이며 후보 설치·정상 측정 복구는 완료하지 않았다.

### 2026-11-01 KST — additional remote recovery attempt

- Exact installed0.2.0 accepts a separate MQTT reboot despite OTA lockout. Owner-authorized one-shot recovery reboot at01:09:04KST uses fresh protected sensor-fault admission, QoS0/non-retained and a durable no-resend intent. Fresh online + uptime12931ms confirms new boot; later retained status uptime300933ms and a fresh collector receipt still report sensor invalid/null voltage. This did not restore measurement or install0.2.1.
- No registered dedicated HA power switch, field Tailnet route/jump host or installed Wi-Fi uploader was found. Scheduled reboot/config/protection release cannot supply the missing OTA admission. Matching installed Arduino3.3.11 SDK default confirms an image before sensor health and no app rollback command exists; actual OTA flash state remains unqueried.
- Keep PR22 Draft/unmerged, version manifest held and power observer active. See [remote path evidence](ina219_regression.md#additional-remote-recovery-investigation-2026-11-01-kst). No further autonomous restart/OTA/motion trial is queued.

### 2026-11-01 — field recovery preparation

Owner requests an offline USB recovery kit; no new remote device action. Correction includes bounded version/typed INA219 Serial diagnostics and an explicit16MB image/bootloader header setting, previously inherited as8MB despite the16MB table. A native-USB field profile enables CDC with zero TX wait; normal UART profile is retained for ordinary builds. Actual hardware flash size, USB enumeration and commissioning remain pending.

The private production bundle is built only on explicit exact-commit dispatch and uploaded only as AES256-GCM CMS ciphertext. The Windows tool defaults to offline verification, snapshots overwritten sectors/settings, writes app0+otadata only and keeps commissioning manual. [Equipment and field procedure](field_recovery.md). PR22 and release hold remain until real field acceptance.
