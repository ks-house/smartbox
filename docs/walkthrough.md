# 🚀 스마트 자동 수거함 최종 구현 완료 보고서 (Walkthrough)

본 문서는 스마트 자동 수거함(SmartBox)의 핵심 S/W 아키텍처(HAL + FSM + TDD) 구축, 동적 감도 및 Preferences 연동, 비동기 웹 관리자 대시보드 및 SmartThings 연동을 넘어 **Web Upload OTA** 및 **HTTPS NAS Pull 방식 OTA**와 **Pre-OTA 하드웨어 안전 셧다운** 기능까지 통합 구현 및 검증을 완료한 최종 보고서입니다.

---

## 🛠️ 주요 구현 내용 및 아키텍처

### 1. 하드웨어 추상화 레이어 (HAL) 및 TDD 환경 구축
- [HardwareInterface.h](../src/HardwareInterface.h)를 통해 하드웨어 제어 인터페이스를 추상화하였습니다.
- [MockHardware.h](../src/MockHardware.h)를 구현하여 실제 하드웨어 없이도 FSM 상태 변화와 물리적 안전 인터락 동작을 PC 로컬 (`native` 환경)에서 유닛 테스트할 수 있도록 지원합니다.
- [Esp32Hardware.cpp](../src/Esp32Hardware.cpp)를 통해 ESP32-C6 타겟 환경의 아두이노 Core API와 INA219 직류 센서 제어를 완벽히 매핑하였습니다.

### 2. 비동기 무선 네트워크 및 SmartThings 연동
- FreeRTOS의 비동기 큐와 백그라운드 우선순위 태스크 구조를 적용하여 Wi-Fi 이벤트 처리와 REST API Webhook 리포팅 지연이 뚜껑 개방 및 모터 과전류 안전 제어 루프를 블로킹하지 않도록 설계 및 구현하였습니다.

### 3. Preferences 연동 및 동적 감도 변경
- 감지 거리, 뚜껑 열림 대기 시간(HOLD), 모터 차단 임계 전류 등의 감도 변수를 웹 인터페이스를 통해 동적으로 변경할 수 있으며, 이 파라미터들은 비휘발성 플래시 영역(Preferences)에 보존됩니다.

### 4. Web OTA 및 HTTPS NAS Pull OTA (Pre-OTA Hardware Interlock)
- **웹 브라우저 파일 업로드 방식(Multipart HTTP POST)**과 **시놀로지 NAS HTTPS Pull 방식(FreeRTOS 백그라운드 태스크 기동 및 HTTPUpdate)** 두 가지 경로를 모두 지원하도록 확장하였습니다.
- 12V 고전류 모터 제어 환경에서의 안전성을 확보하기 위해, 펌웨어 쓰기 개시 즉시 모든 릴레이 전원을 완전 격리하고 FSM을 동결하는 Pre-OTA 안전 셧다운을 설계 및 통합하였습니다.

---

## 🚨 Pre-OTA Hardware Interlock 안전 메커니즘

```
[Web Upload /api/ota 첫 청크 수신] 또는 [NAS Pull /api/update-from-nas 요청 수신]
                                │
                                ▼
  1. forceAllRelaysOff() 호출
     - GPIO 6, 7, 8 제어 핀을 즉시 INPUT(고임피던스) 모드로 전환
     - 릴레이 보드로 흐르는 대기 전류를 0mA로 리셋하여 하드웨어 완전 격리
                                │
                                ▼
  2. transitionTo(STATE_OTA_UPDATING) 호출
     - FSM 상태를 OTA 모드로 고정하여 센서 입력 및 오작동에 의한 상태 전이 원천 차단
                                │
                                ▼
  3. OTA 다운로드 및 플래시 기록 시작
     - Web Upload: AsyncWebServer Multipart 콜백에서 패킷 chunk 기록
     - NAS Pull: WiFiClientSecure(setInsecure) + HTTPUpdate를 백그라운드 태스크에서 기동
```

---

## 📂 최종 수정 및 보강된 파일 명세

1. **[SmartBoxController.h](../src/SmartBoxController.h) & [SmartBoxController.cpp](../src/SmartBoxController.cpp)**:
   - `State` 열거형에 `STATE_OTA_UPDATING` 추가.
   - `update()` 최상단에 OTA 상태 가드를 배치하여 FSM 동작 및 센서 스캔 격리.
   - `forceAllRelaysOff()`와 `transitionTo()` 메서드를 `public`으로 승격하여 OTA 콜백 및 FreeRTOS 태스크에서 즉시 호출되도록 조치.
2. **[WebDashboard.h](../src/WebDashboard.h) & [WebDashboard.cpp](../src/WebDashboard.cpp)**:
   - `POST /api/ota` 핸들러에서 로컬 웹 파일 업로드 지원 및 안전 인터락 작동.
   - `GET /api/update-from-nas` 엔드포인트 및 `nasOtaTaskFunction` FreeRTOS 태스크(Stack 16KB) 추가.
   - `WiFiClientSecure`의 `client.setInsecure()`를 적용하여 NAS HTTPS 루트 인증서 검증 바이패스.
   - `httpUpdate.update(client, URL)`을 연동해 백그라운드 펌웨어 다운로드 및 쓰기 구현.
   - 웹 UI 대시보드 하단에 **"Firmware Update (from NAS)"** 카드 추가 및 Fetch & Update 인터랙션과 시각적 피드백 제공.
3. **[platformio.ini](../platformio.ini)**:
   - 16MB 플래시 용량을 충분히 활용하기 위한 `board_build.partitions = default_16MB.csv` 파티션 설정 추가.
4. **[main.cpp](../src/main.cpp)**:
   - `loop()` 내에서 `isOtaMode()`가 활성화되면 비동기 웹서버의 패킷 처리를 위해 `delay(100)`으로 양보하여 Watchdog Timer(WDT) 트리거 방지.
5. **[test_main.cpp](../test/test_native/test_main.cpp)**:
   - `test_ota_state_isolation()` 단위 테스트 추가를 통해 릴레이 셧다운 모드 진입 무결성 검증.

---

## 🔍 검증 결과

### 1. 자동화 테스트 결과 (Native 유닛 테스트)
```powershell
pio test -e esp32-c6-test
```
- FSM 상태 전이, 100ms 안전 딜레이 가드, 과전류 차단, 저전압 강제 개방 셧다운, 중간값 노이즈 필터, 동적 감도 저장 설정 검증에 이어 신규 추가된 **OTA 하드웨어 격리 및 상태 동결 유닛 테스트까지 100% 통과(PASSED)**했습니다.

### 2. 타겟 보드 빌드 상태
- **`esp32-c6-devkitc-1`**: ✅ 빌드 성공 (RAM 13.9%, Flash 19.3% 점유)
- **`esp32-c6-test`**: ✅ 테스트 빌드 및 유닛 테스트 통과
