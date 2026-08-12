# AGENTS.md — SmartBox collaboration guide

이 문서는 SmartBox 저장소를 수정하는 모든 AI 에이전트의 작업 규칙이다.

## 1. 시작 순서

1. [wiki/index.md](wiki/index.md)를 읽는다.
2. [wiki/log.md](wiki/log.md)의 최근 항목을 읽는다.
3. 최신 source와 tests를 문서보다 우선해 확인한다.
4. 코드나 운영 계약을 바꾸면 관련 wiki와 현재형 매뉴얼을 함께 갱신한다.
5. 새 문서를 index에 연결하고 log에 append한다.
6. 상대 링크, 비밀 노출, 문서와 코드의 모순을 검사한다.

## 2. 프로젝트와 기준 환경

- 제품: ESP32-C6 기반 자동 수거함 뚜껑 제어기
- MCU: ESP32-C6-DevKitC-1, RISC-V, 16 MB flash
- 빌드: PlatformIO + pioarduino Arduino framework
- 핵심 원칙: 12 V 고전류 부하와 기계식 릴레이의 안전이 기능보다 우선한다.
- 현재 상태와 열린 검증 항목: [wiki/project_status.md](wiki/project_status.md)
- 상세 구조: [wiki/architecture.md](wiki/architecture.md)

## 3. 현재 핀 매핑

| 기능 | GPIO | 코드 근거 |
|---|---:|---|
| AJ-SR04T TRIG | 4 | `src/main.cpp`, `src/SmartBoxController.cpp` |
| AJ-SR04T ECHO | 5 | `src/main.cpp`, `src/SmartBoxController.cpp` |
| Main relay | 6 | `src/SmartBoxController.cpp` |
| Direction relay A / open | 7 | `src/SmartBoxController.cpp` |
| Direction relay B / close | 8 | `src/SmartBoxController.cpp` |
| INA219 SDA | 22 | `src/Esp32Hardware.cpp` |
| INA219 SCL | 23 | `src/Esp32Hardware.cpp` |

핀을 변경하면 source, [docs/WIRING_GUIDE.md](docs/WIRING_GUIDE.md), [docs/DEVELOPMENT_MANUAL.md](docs/DEVELOPMENT_MANUAL.md), wiki와 tests를 같은 변경에서 동기화한다.

## 4. 릴레이 안전 불변조건

현재 릴레이 모듈은 **Active-Low**로 구현되어 있다. 논리적 `dirOpen`과 `dirClose`가 동시에 true가 되는 것을 금지한다.

- Direction A ON: GPIO 7 LOW, GPIO 8 HIGH
- Direction B ON: GPIO 7 HIGH, GPIO 8 LOW
- Direction neutral: GPIO 7 HIGH, GPIO 8 HIGH
- Main ON: GPIO 6 OUTPUT LOW
- Main OFF: GPIO 6 INPUT high-impedance
- 완전 격리: GPIO 6, 7, 8을 INPUT으로 전환

방향을 바꿀 때는 반드시 다음 순서를 보존한다.

```text
Main INPUT/OFF
→ 100 ms 접점 분리
→ direction 변경
→ 100 ms 안정화
→ Main OUTPUT LOW/ON
```

다음 변경은 금지한다.

- `dirOpen && dirClose` 동시 허용
- Main 전원을 켠 상태에서 방향 접점 변경
- 부팅 시 방향 핀을 먼저 LOW로 구동
- OTA 중 relay/FSM 구동 재개
- 액추에이터 시간 제한 제거

코드의 최종 안전 근거는 `SmartBoxController::setRelayStates()`와 `forceAllRelaysOff()`다.

## 5. 소프트웨어 불변조건

- 네트워크 기능이 활성화된 main loop는 비차단 FSM과 `millis()` 타이머를 유지한다.
- 물리 접점 분리를 위한 `setRelayStates()` 내부 100 ms 대기는 안전 지연이므로 일반적인 비차단 규칙보다 우선한다.
- FSM 상태 전이는 Serial에 기록한다.
- OTA 시작 전 relay를 격리하고 `STATE_OTA_UPDATING` 동안 센서/액추에이터 FSM을 멈춘다.
- 16 MB OTA 파티션 계약을 유지한다. `default_16MB.csv`를 임의로 제거하지 않는다.
- MQTT 연결 수락·publish 반환은 물리 동작 완료 증거가 아니다.
- 저장소 구현, 테스트로 검증된 사실, 현장 배포 상태를 구분해 문서화한다.

## 6. 비밀과 링크

- Wi-Fi, MQTT, NAS, 인증서와 계정 정보는 `include/secrets.h`에만 두고 Git에 포함하지 않는다.
- 예시는 `include/secrets.h.example`의 더미 값만 사용한다.
- 콘솔, 문서, Git remote URL에 실제 값을 출력하거나 기록하지 않는다.
- 저장소 Markdown은 표준 상대 링크만 사용한다. `file:///` 또는 로컬 절대 경로를 커밋하지 않는다.

## 7. 문서 정책

- `wiki/`는 현재 사실, 운영 계약, 지식 라우팅의 진실 공급원이다.
- `reports/`와 `prompts/`는 시점별 역사 자료이며 현재 구현으로 자동 해석하지 않는다.
- `docs/implementation_plan.md`, `docs/task.md`, `docs/walkthrough.md`, `docs/mqtt_implementation_plan.md`는 완료된 작업 snapshot이다.
- 기존 역사 자료를 현재형으로 덮어쓰지 말고 `status: superseded`와 후속 문서 링크를 사용한다.
- `wiki/log.md`는 append-only다.
- 여러 프로젝트에서 재사용할 수 있는 원칙만 Knowledge Hub로 승격하며 프로젝트별 값과 배포 사실은 이 저장소에 남긴다.

## 8. 검증

문서 변경이라도 최소한 다음을 확인한다.

```powershell
pio test -e native
pio run -e esp32-c6-devkitc-1
```

PlatformIO가 PATH에 없거나 target build가 환경 문제로 불가능하면 실패 원문과 실행하지 못한 범위를 `wiki/log.md`에 기록한다. 실기기 검증 없이 build 성공을 하드웨어 검증으로 표현하지 않는다.
