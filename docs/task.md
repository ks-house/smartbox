# 스마트 자동 수거함 개발 Task List

본 문서는 TDD(RED-GREEN-REFACTOR) 방법론에 맞추어 다음 개발 단계(AI 에이전트)에서 차례대로 완수해야 할 상세 구현 항목들을 정의한 TODO 리스트입니다.

---

## Phase 1: Core FSM & TDD (기본 안전 제어 구현)

- [ ] **1단계: 환경 설정 및 HAL 구조 수립**
  - [ ] [platformio.ini](../platformio.ini)에 `[env:native]` 테스트 실행 환경 설정 추가
  - [ ] [HardwareInterface.h](../src/HardwareInterface.h) [NEW]: 추상 하드웨어 API 정의 (GPIO 제어, 시간 측정, 센서 인터페이스)
  - [ ] [MockHardware.h](../src/MockHardware.h) [NEW]: 유닛 테스트 검증용 모의 객체(Mock) 구현 (가상의 시간 흐름, 핀 상태 기록, 센서 피드백 조작 기능)
  - [ ] [Esp32Hardware.h](../src/Esp32Hardware.h) [NEW], [Esp32Hardware.cpp](../src/Esp32Hardware.cpp) [NEW]: 아두이노 프레임워크 및 실제 하드웨어 바인딩 구현

- [ ] **2단계: TDD 1 - 부팅 시 릴레이 글리치 차단 및 초기 안전 상태 검증**
  - [ ] `test/test_native/test_main.cpp`에 [RED] 유닛 테스트 작성: `SmartBoxController::init()` 시점에 릴레이 세 핀(`GPIO 6, 7, 8`)이 즉시 `INPUT`(고임피던스) 상태로 설정되는지 검증
  - [ ] [SmartBoxController.cpp](../src/SmartBoxController.cpp) [NEW] 구현을 통해 [GREEN] 테스트 통과: 아키텍처에 정의된 글리치 방지 모드로 핀 초기 설정
  - [ ] [REFACTOR] 핀 정의 및 상수 매핑 데이터 구조 정리

- [ ] **3단계: TDD 2 - 릴레이 인터락 및 100ms 가드 타임 검증**
  - [ ] `test/test_native/test_main.cpp`에 [RED] 유닛 테스트 작성:
    - `GPIO 7(IN1)`과 `GPIO 8(IN2)`이 동시에 `LOW`(ON)로 활성화되지 않음을 증명
    - 모터 방향 전환(`OPEN_START` 및 `CLOSE_START`로의 상태 전이) 시, 메인 전원(`GPIO 6`)이 차단된 상태에서 최소 100ms 대기(안전 가드)한 후에 방향 전환을 지시하는지 검증
  - [ ] [SmartBoxController.cpp](../src/SmartBoxController.cpp) 구현을 통해 [GREEN] 테스트 통과: `millis()` 기반 비동기 대기 상태 전이 구현
  - [ ] [REFACTOR] 지연 및 상태 제어 함수 모듈화로 가독성 향상

- [ ] **4단계: TDD 3 - 과전류 감지 시 비상 정지(EMERGENCY_STOP) 검증**
  - [ ] `test/test_native/test_main.cpp`에 [RED] 유닛 테스트 작성:
    - 동작 시작 초기 300ms(기동 전류) 구간 내에서 과전류 한계치 초과는 감지에서 제외
    - 300ms 이후 임계값을 넘는 부하 전류가 감지되면 즉시 `EMERGENCY_STOP` 상태로 전이되고 모든 릴레이가 차단(INPUT/고임피던스)되는지 검증
  - [ ] [SmartBoxController.cpp](../src/SmartBoxController.cpp) 구현을 통해 [GREEN] 테스트 통과: 과전류 모니터링 스레드 시뮬레이션 및 상태 전이 구현
  - [ ] [REFACTOR] 전류 임계치 및 기동 시간 상수를 제어기 내 설정 변수(`BoxConfig`)로 추상화

- [ ] **5단계: TDD 4 - 저전압 셧다운(BATTERY_LOW_SHUTDOWN) 검증**
  - [ ] `test/test_native/test_main.cpp`에 [RED] 유닛 테스트 작성:
    - 배터리 전압이 11.3V 이하로 3초 연속 유지될 때, 안전 개방(`OPEN_START`) 루틴을 수행한 뒤 모든 릴레이 제어 핀을 `INPUT`(고임피던스)으로 전환하고 무한 루프로 정지하는지 검증
    - 3초 미만 일시적인 전압 강하 상황에서는 작동하지 않음을 증명
  - [ ] [SmartBoxController.cpp](../src/SmartBoxController.cpp) 구현을 통해 [GREEN] 테스트 통과: 저전압 축적 타이머 및 동결 시퀀스 구현
  - [ ] [REFACTOR] 누적 시간 카운팅 로직의 모듈성 확보

- [ ] **6단계: TDD 5 - 초음파 거리 센서 중간값 필터 검증**
  - [ ] `test/test_native/test_main.cpp`에 [RED] 유닛 테스트 작성:
    - 50ms 마다 수집되는 최근 5개의 초음파 거리 원시 데이터 중 일시적인 오인식 노이즈(튀는 값)가 들어와도 필터를 거친 중간값에 의해 정확히 상태 전이가 결정되는지 검증
  - [ ] [SmartBoxController.cpp](../src/SmartBoxController.cpp) 구현을 통해 [GREEN] 테스트 통과: 최근 5개 샘플에 대한 중간값(Median) 정렬 알고리즘 적용
  - [ ] [REFACTOR] 삽입 정렬 등 효율적인 버퍼 핸들링 리팩토링

---

## Phase 2: 설정 저장 및 이벤트 아키텍처 (TDD 확장)

- [ ] **7단계: TDD 6 - 감도 조절 동적 변경 및 이벤트 콜백 리포트 검증**
  - [ ] `test/test_native/test_main.cpp`에 [RED] 유닛 테스트 작성:
    - 제어기의 `BoxConfig` 설정을 실시간 변경했을 때(감지 거리, HOLD 시간 등) 실제 FSM 임계 로직이 업데이트된 감도로 즉시 판정하는지 검증
    - FSM 상태 변경이 일어날 때, 등록한 콜백 함수가 이전/이후 상태 정보를 담아 올바르게 호출되는지 검증
  - [ ] [SmartBoxController.cpp](../src/SmartBoxController.cpp) 구현을 통해 [GREEN] 테스트 통과: `BoxConfig` 변경 바인딩 및 상태 천이 함수 내 콜백 트리거 코드 추가
  - [ ] [REFACTOR] 제어기 내에서 설정 및 콜백 처리 구조 최적화

---

## Phase 3: 비동기 통신 & 설정 저장 구현 (Target)

- [x] **8단계: 비휘발성 저장 연동**
  - [x] [ConfigManager.h](../src/ConfigManager.h) [NEW] 및 [ConfigManager.cpp](../src/ConfigManager.cpp) [NEW]: ESP32 `Preferences` 라이브러리를 바인딩하여 설정값 로드 및 저장 구현
  - [x] 시스템 부팅 시 비휘발성 저장 영역에서 설정을 로드하고 `SmartBoxController`에 주입하는 플로우 생성

- [x] **9단계: 비동기 Wi-Fi 및 SmartThings API 연동**
  - [x] [WifiManager.h](../src/WifiManager.h) [NEW] / [WifiManager.cpp](../src/WifiManager.cpp) [NEW]: 아두이노 비동기 와이파이 이벤트 등록 및 연결 관리 기능 구현
  - [x] [SmartThingsReporter.h](../src/SmartThingsReporter.h) [NEW] / [SmartThingsReporter.cpp](../src/SmartThingsReporter.cpp) [NEW]:
    - [x] `SmartBoxController`의 상태 변경 콜백을 수신하여 이벤트를 큐에 Enqueue
    - [x] 백그라운드 FreeRTOS 태스크를 구동하여 큐에서 이벤트를 꺼내 비동기로 SmartThings REST API Webhook 전송을 처리하도록 구현

- [x] **10단계: 비동기 웹 관리자 서버 및 실시간 WebSocket 통신**
  - [x] `ESPAsyncWebServer` 구동 및 기본 관리자용 HTML/JS/CSS 소스 바인딩 (Web UI 포함)
  - [x] WebSocket API 핸들러 작성: 실시간 센서값(거리, 전류, 전압) 및 FSM 상태 데이터를 500ms~1000ms 간격으로 브로드캐스트
  - [x] 관리자 조정 요청 REST API 핸들러 작성:
    - [x] 감도 설정 변경 시, 신규 파라미터를 파싱하여 `ConfigManager` 및 `SmartBoxController`에 실시간 업데이트하고 Preferences에 저장
    - [x] 강제 제어 명령(원격 열기, 비상 정지 해제) 수신 시 `SmartBoxController::forceOpen()` 또는 `SmartBoxController::resetEmergency()` 호출 연동

---

## Phase 4: 통합 및 최종 실물 보드 검증

- [ ] **11단계: 메인 통합**
  - [ ] [main.cpp](../src/main.cpp) [MODIFY]:
    - `Esp32Hardware` 인스턴스화 및 `SmartBoxController` 생성
    - `ConfigManager`를 통한 초기 세팅 인입
    - `WifiManager` 및 `SmartThingsReporter` 구동 및 FSM 상태 콜백 주입
    - 비동기 웹서버 시작
  - [ ] 전체 빌드 및 Native 테스트 재수행 검증
- [ ] **12단계: 타겟 보드 업로드 및 실물 검증**
  - [ ] ESP32-C6 타겟 보드 업로드
  - [ ] 시리얼 로그 확인을 통한 동작 상태 검증
  - [ ] 모바일 기기로 웹 어드민 페이지에 접속하여 실시간 차트/모니터링, 감도 입력 수정 저장 및 원격 개방이 비동기적으로 지연 없이 안전하게 연동되는지 종합 시험 진행
