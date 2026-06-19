# 📋 26061905_WebOTA통합_report.md
    
- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 19일
- **주제:** WebOTA통합
    
---
    
## 1. 이슈 개요 (Issue Overview)
태양광 스마트 자동 수거함(SmartBox)의 유지보수 편의성을 극대화하기 위해, 무선 네트워크를 통한 펌웨어 업데이트 기능(Web OTA)이 요구되었습니다.
동작 특성상 12V 고전류 리니어 액추에이터와 물리적인 기계식 릴레이(H-브릿지 구조)를 제어하므로, 펌웨어 업로드 과정이나 리부팅 시의 GPIO 전위 불안정(부팅 글리치) 및 FSM 상태 불안정으로 인해 릴레이가 단락(쇼트)되거나 액추에이터가 예기치 않게 구동될 수 있는 치명적인 위험이 존재합니다.
따라서, **하드웨어 단락 및 오작동을 원천 봉쇄하는 Pre-OTA 하드웨어 안전 셧다운 인터락**을 설계하고, 비동기 웹 대시보드([WebDashboard.cpp](../src/WebDashboard.cpp))와 상태 머신([SmartBoxController.cpp](../src/SmartBoxController.cpp))에 안정적으로 통합 구현하는 작업을 수행했습니다.
    
## 2. 원인 분석 (Root Cause Analysis)
1. **플래시 기록 중 제어 루프의 간섭**: Web OTA 업로드 중 CPU가 플래시에 펌웨어 데이터를 기록하는 동안, 기존의 FSM 제어 루프(초음파 거리 측정, 배터리 감시 등)가 병행 동작하면 데이터 손실, 펌웨어 쓰기 실패, 혹은 예기치 못한 상태 변화로 인한 릴레이 구동 명령이 전송될 수 있습니다.
2. **리부팅 시 GPIO 글리치**: OTA 완료 직후 `ESP.restart()`가 실행되는 과정에서 GPIO 6, 7, 8번 핀의 전위가 불안정해져 릴레이가 일시적으로 활성화되면 12V 단락이나 모터의 불규칙 구동이 발생할 수 있습니다.
3. **OTA 파티션 부재**: 기존 파티션 설계로는 OTA를 위한 듀얼 앱 영역(OTA0, OTA1) 및 데이터 영역을 할당하기에 용량이 부족할 수 있으므로, ESP32-C6의 16MB Flash 스펙에 맞춘 파티션 스키마 변경이 선행되어야 합니다.
    
## 3. 해결 설계 및 구현 (Solution Design & Implementation)
이 문제를 해결하기 위해 **Pre-OTA Hardware Interlock(선-셧다운 후-업데이트)** 메커니즘을 설계 및 구현했습니다.

### 3.1. FSM 상태 추가 및 제어 격리
- `SmartBoxController`의 `State` 열거형에 `STATE_OTA_UPDATING` 상태를 추가했습니다.
- [SmartBoxController.cpp](../src/SmartBoxController.cpp)의 `update()` 루프 최상단에 Early Return 가드를 배치하여 `STATE_OTA_UPDATING`일 때는 FSM과 센서 폴링이 완전히 동결되도록 설계했습니다.
- [main.cpp](../src/main.cpp)의 `loop()`에서도 `isOtaMode()`가 감지되면 FSM 업데이트를 스킵하고 `delay(100)`을 수행하여 CPU 자원을 비동기 웹서버의 OTA 프로세스에 100% 양보하도록 격리했습니다.

### 3.2. Pre-OTA 릴레이 차단 인터락
- [WebDashboard.cpp](../src/WebDashboard.cpp)의 `POST /api/ota` API 핸들러의 첫 번째 청크(`index == 0`) 수신 시, 즉시 아래 동작을 원자적으로 수행합니다:
  1. `forceAllRelaysOff()` 호출: `GPIO 6, 7, 8` 제어 핀을 `INPUT`(고임피던스) 상태로 긴급 전환하여 릴레이 보드로 흐르는 전류를 물리적으로 완전히 차단(대기 전력 0mA).
  2. `transitionTo(STATE_OTA_UPDATING)` 호출: FSM 상태를 즉시 OTA 모드로 고정하여 다른 상태 전이를 차단.
  3. `Update.begin()` 실행: 하드웨어가 완전히 격리된 상태에서 비로소 플래시 쓰기를 개시.

### 3.3. 비동기 웹 대시보드 UI 연동
- Web UI에 펌웨어 업로드를 위한 전용 모던 카드 및 드래그 앤 드롭 지원 파일 선택기를 내장했습니다.
- XHR 기반의 실시간 프로그레스 바(애니메이션 효과 포함)를 구현하여 전송 상태를 사용자에게 동적으로 표시합니다.
- 업로드 성공 시 브라우저에서 10초 카운트다운을 표시하며 자동 페이지 리로드를 대기하고, 백엔드는 안전 딜레이(`1500ms`)를 거쳐 `ESP.restart()`를 호출해 안전하게 리부팅합니다.

### 3.4. 파티션 구성 변경
- [platformio.ini](../platformio.ini) 파일에 `board_build.partitions = default_16MB.csv` 설정을 추가하여 16MB Flash 내에 안정적인 듀얼 OTA 펌웨어 업로드 영역을 확보했습니다.

---

## 4. 검증 결과 (Verification Results)

### 4.1. 유닛 테스트를 통한 로직 및 인터락 검증
- [test_main.cpp](../test/test_native/test_main.cpp)에 `test_ota_state_isolation()` 단위 테스트를 작성하여 아래 시나리오를 통과시켰습니다:
  - 컨트롤러가 `STATE_OPENING`인 상태(모터 구동 중)에서 OTA 시작 조건 시뮬레이션.
  - `forceAllRelaysOff()` 및 `transitionTo(STATE_OTA_UPDATING)` 호출 검증.
  - 단언(Assertion): 릴레이 3개 핀이 모두 `INPUT` 모드로 전환되었는지 확인.
  - FSM 격리 검증: `STATE_OTA_UPDATING` 진입 후 초음파 센서로 사람 접근 노이즈를 주입해도 상태가 변하지 않고 고정되는지 확인.
  - `isOtaMode()` 반환값 검증.
- **결과**: `native` 환경에서 테스트 빌드 성공 및 100% 통과 완료.

### 4.2. 타겟 보드 컴파일 및 빌드 결과
두 가지 환경에 대해 PlatformIO 빌드를 수행하였으며, 모두 성공했습니다.
- **`esp32-c6-devkitc-1`**: SUCCESS
  - RAM 사용량: 13.9% (45.5KB / 320KB)
  - Flash 사용량: 19.0% (1.25MB / 6.55MB)
- **`esp32-c6-test`**: PASSED (native 테스트 통과)
