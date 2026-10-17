---
title: SmartBox current project status
type: reference
project: smartbox
status: active
updated: 2026-08-12
source_of_truth: true
applies_to:
  - firmware
  - hardware
  - mqtt
  - ota
---

# 현재 프로젝트 상태

> Repository baseline: `9f2facbec58d47b0372677bcdf51cc853a00944d`
>
> 이 문서는 checkout에서 확인한 구현을 설명한다. 현장 설치 또는 실기기 합격을 자동으로 뜻하지 않는다.

## 한눈에 보기

| 영역 | 저장소 최신 구현 | 검증 경계 |
|---|---|---|
| Controller | 11-state FSM, 5-sample median, debounce, post-close blind time, deadlock self-healing | native test 대상이나 실제 센서·기구 반복 시험은 별도 |
| Actuator | Active-Low relay, Main high-impedance cutoff, 100 ms 방향 전환 interlock, 3.8 s 기본 timeout | GPIO/접점/부하/과전류 실측 필요 |
| Startup | NVS가 lid-open을 기록한 경우 `STARTUP_OPEN → OPEN_START → OPENING → HOLD → CLOSE_START → CLOSING → IDLE` 보정 | 모든 부팅에서 보정하는 구현은 아님 |
| Wi-Fi | 부팅 후 5분 AP+STA captive portal, 이후 STA, 15초 연결 재시도 | AP/OEM/router 현장 복구 증거 별도 |
| MQTT | PsychicMqttClient, MQTTS URI, Root CA, command/config, LWT, HA discovery 20개 | broker 수락과 물리 동작 완료는 별개 |
| Telemetry | 30초 live, motor 500 ms batch, 1분 heartbeat sample, 설정 주기 batch | timestamp는 `millis()`이며 wall-clock epoch가 아님 |
| OTA | 부팅 1회 확인, `otaHour` 일 1회 확인, HTTPS Root CA, pre-OTA relay isolation | signature, rollback, post-reboot health confirmation은 확인되지 않음 |
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
