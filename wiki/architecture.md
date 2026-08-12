---
title: SmartBox architecture
type: reference
project: smartbox
status: active
updated: 2026-08-12
source_of_truth: true
---

# 아키텍처

## 실행 흐름

```text
AJ-SR04T + INA219
  → Esp32Hardware
  → SmartBoxController FSM
  → Active-Low relay interlock
  → dual 12 V linear actuators

Wi-Fi AP/STA
  → PsychicMqttClient over MQTTS
  → command/config + Home Assistant discovery
  → FSM methods

NetworkTask
  → AutoOtaManager
  → TelemetryManager
  → MqttManager
```

Arduino `loopTask`는 FSM, 거리/전류/전압, Wi-Fi provisioning 상태, PowerManager를 처리한다. 별도 16 KB `NetworkTask`는 OTA scheduler, telemetry, MQTT와 watchdog feed를 100 ms 주기로 처리한다.

## Hardware boundary

### 입력

- AJ-SR04T: GPIO 4 TRIG, GPIO 5 ECHO, `pulseIn(..., 30000)` timeout, 오류 sentinel 999 cm.
- INA219: `Wire.begin(22, 23)`. 감지 실패 시 코드가 nominal 12.2 V와 0 mA를 반환하므로 “센서 없음”과 “정상 전원”이 telemetry에서 구분되지 않는 한계가 있다.

### 출력과 relay

논리 API는 `setRelayStates(mainOn, dirOpen, dirClose)`다. 물리 출력은 Active-Low다.

| 논리 상태 | GPIO 6 Main | GPIO 7 A | GPIO 8 B |
|---|---|---|---|
| open | OUTPUT LOW | LOW | HIGH |
| close | OUTPUT LOW | HIGH | LOW |
| neutral | INPUT | HIGH | HIGH |
| isolated | INPUT | INPUT | INPUT |

방향 변화 시 Main을 먼저 INPUT으로 만들고 100 ms씩 두 번 기다린 뒤 Main을 켠다. `dirOpen && dirClose` 요청은 `EMERGENCY_STOP`으로 fail closed한다.

## FSM boundary

- IDLE: 필터 거리와 debounce를 만족하면 open.
- OPENING: 3.8 s 기본 timeout. 시작 500 ms 이후 stall 3회는 open limit로 보고 HOLD.
- HOLD: 기본 10 s, 접근 재감지 시 timer 연장, 최대 3분에서 강제 close.
- CLOSING: 사람 감지 시 reopen. stall 3회는 emergency stop.
- EMERGENCY_STOP: relay off, 기본 5 s 후 복구.
- BATTERY_LOW_SHUTDOWN: 저전압 3초 후 open 구동, timeout 뒤 isolation.
- MAINTENANCE: open 유지, 5분 뒤 close.
- OTA_UPDATING: relay isolation과 FSM 정지.
- STARTUP_OPEN: NVS lid-open boot에서 500 ms 후 open/hold/close calibration path 시작.

## Network boundary

부팅 시 `SmartBox-WiFi` AP와 STA를 함께 시작한다. captive portal은 5분 뒤 닫히고 STA만 유지된다. 저장된 SSID가 있으면 15초 단위로 재연결한다.

MQTT는 `mqtts://<SECRET_MQTT_HOST>:<SECRET_MQTT_PORT>`와 Root CA를 사용한다. fixed client/device identity는 `ESP32C6_SMARTBOX_01` / `smartbox_01`이다. 명령 topic은 `smartbox/command` 단일 namespace다.

지원 command:

- `force_open`, `force_close`
- `reset_emergency` / `release_emergency`
- `maintenance` start/stop
- `reboot`
- `trigger_ota`
- `debug` on/off
- `set_config`

현재 코드에는 command signature, nonce/replay guard, per-device ACL 검증이 없다. broker와 network ACL이 외부 trust boundary다.

## Telemetry boundary

- `smartbox/telemetry`: 30초 live snapshot.
- `smartbox/telemetry/batch`: motor event 500 ms samples 또는 1분 heartbeat samples.
- `smartbox/event/state`, `/motion`, `/alarm`, `/cycle`: 상태·사건.
- `smartbox/status`: retained online/LWT offline.
- `smartbox/config/state`: retained config snapshot.
- `smartbox/log`: 기본 WARN/ERROR, debug mode에서 전체 로그.

payload의 `timestamp`와 `ts`는 wall-clock epoch가 아니라 `millis()` 기반 boot-relative 값이다.

## OTA boundary

1. 부팅 후 첫 scheduler tick에서 version manifest를 확인한다.
2. NTP가 준비되면 `otaHour`에 일 1회 확인한다.
3. remote version 문자열이 current version과 다르면 relay를 격리한다.
4. `STATE_OTA_UPDATING`으로 전이하고 HTTPS firmware를 설치한다.
5. 성공 시 reboot, 실패 시 WDT를 복구하고 IDLE로 돌아간다.

TLS Root CA 검증은 구현돼 있지만 firmware signature, rollback slot 선택, post-reboot health mark는 코드에서 확인되지 않는다.

## CI/CD boundary

`main` push는 native test 후 target build를 수행하고 `firmware.bin`과 `version.json`을 SFTP로 배포한다. CI build 성공은 장치 설치·reboot·health 증거가 아니다.
