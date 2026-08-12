# SmartBox

ESP32-C6, AJ-SR04T, INA219, Active-Low relay와 12 V linear actuator 두 개를 사용하는 자동 수거함 뚜껑 controller다. 현재 checkout의 기준과 남은 검증 항목은 [wiki/project_status.md](wiki/project_status.md), 전체 문서 지도는 [wiki/index.md](wiki/index.md)를 따른다.

> Repository baseline: `main` commit `9f2facb` (2026-07-23)
>
> 저장소에 구현된 기능은 실기기 검증 또는 현장 배포 완료를 자동으로 뜻하지 않는다.

## 현재 구현

- 11-state non-blocking FSM과 Active-Low relay interlock.
- 거리 5-sample median, debounce, 3초 post-close blind time, 최대 HOLD와 sensor deadlock self-healing.
- INA219 전압/전류 감시, open/close stall 처리, 11.3 V 기본 저전압 보호.
- 부팅 5분 captive portal과 저장된 STA 자동 재연결.
- PsychicMqttClient 기반 MQTTS, Root CA, LWT, 원격 command/config, Home Assistant discovery 20개.
- 30초 live telemetry, motor-event batch, 1분 heartbeat sample과 설정 주기 batch.
- 부팅 1회 + 설정한 `otaHour` 일 1회 HTTPS OTA 확인, pre-OTA relay isolation.
- `(otaHour + 1) % 24` 시각의 일 1회 proactive reboot.
- native FSM tests와 main push target build/SFTP deployment workflow.

## 안전 불변조건

현재 relay는 Active-Low다. 논리적 open 방향과 close 방향을 동시에 활성화하면 안 된다.

| 상태 | Main GPIO 6 | Direction A GPIO 7 | Direction B GPIO 8 |
|---|---|---|---|
| open | OUTPUT LOW | LOW | HIGH |
| close | OUTPUT LOW | HIGH | LOW |
| neutral | INPUT | HIGH | HIGH |
| full isolation | INPUT | INPUT | INPUT |

방향을 바꿀 때는 `Main OFF/INPUT → 100 ms → 방향 변경 → 100 ms → Main ON/LOW` 순서를 보존한다. 상세 계약은 [AGENTS.md](AGENTS.md)와 [개발 매뉴얼](docs/DEVELOPMENT_MANUAL.md)을 따른다.

## Hardware

| 기능 | 연결 |
|---|---|
| AJ-SR04T TRIG / ECHO | GPIO 4 / GPIO 5 |
| Main / direction A / direction B relay | GPIO 6 / GPIO 7 / GPIO 8 |
| INA219 SDA / SCL | GPIO 22 / GPIO 23 |
| MCU | ESP32-C6-DevKitC-1, 16 MB flash |

AJ-SR04T ECHO가 5 V logic을 출력하는 구성이라면 ESP32-C6 GPIO 5에 직접 연결하지 말고 3.3 V 입력 보호를 적용한다. 실제 배선은 [docs/WIRING_GUIDE.md](docs/WIRING_GUIDE.md), 부품은 [HARDWARE_BOM.md](HARDWARE_BOM.md)를 확인한다.

## Build

PowerShell 예시:

```powershell
Copy-Item include/secrets.h.example include/secrets.h
pio test -e native
pio run -e esp32-c6-devkitc-1
```

`include/secrets.h`에는 실제 Wi-Fi, AP, MQTT, HTTPS endpoint와 Root CA를 넣으며 Git에 포함하지 않는다. target build는 `default_16MB.csv` partition을 사용한다.

PlatformIO가 PATH에 없다면 설치 위치의 실행 파일로 같은 명령을 실행한다. build 성공은 flash, relay polarity, physical load, OTA reboot/health 검증을 대신하지 않는다.

## Runtime architecture

```text
AJ-SR04T / INA219
  → SmartBoxController FSM
  → relay safety interlock
  → dual actuator

Wi-Fi AP/STA
  → MQTTS
  → Home Assistant / operator command

NetworkTask
  → AutoOtaManager + TelemetryManager + MqttManager
```

자세한 FSM, topics, command, OTA와 신뢰 경계는 [wiki/architecture.md](wiki/architecture.md)에 있다.

## OTA와 배포 경계

OTA는 HTTPS Root CA 검증과 firmware flashing 전 relay isolation을 수행한다. 다만 현재 코드에서 signed artifact, dual-slot rollback 선택, post-reboot health mark는 확인되지 않는다. `HTTP_UPDATE_OK` 또는 SFTP upload만으로 현장 업데이트 완료라고 기록하지 않는다.

GitHub Actions는 `main` push에서 native test → target build → `firmware.bin`/`version.json` 생성 → SFTP 배포를 수행한다. 운영 환경의 승인과 복구 정책은 CI 성공과 별개다.

## Documentation

- [Wiki index](wiki/index.md)
- [Current status](wiki/project_status.md)
- [Architecture](wiki/architecture.md)
- [User manual](docs/USER_MANUAL.md)
- [Development manual](docs/DEVELOPMENT_MANUAL.md)
- [Wiring guide](docs/WIRING_GUIDE.md)
- [Knowledge management](wiki/knowledge_management.md)

`reports/`와 `prompts/`는 시점별 역사 자료다. 완료된 plan/task/walkthrough 문서보다 현재 wiki와 source를 우선한다.
