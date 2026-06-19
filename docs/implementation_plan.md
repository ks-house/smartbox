# HTTPS NAS 연동 OTA 및 펌웨어 버전 시각화 구현 계획

본 계획서는 스마트 자동 수거함(SmartBox)의 배포 추적성을 위해 현재 펌웨어 버전을 명시 및 시각화하고, 기존 구현된 HTTPS NAS Pull OTA 및 Pre-OTA 안전 인터락을 확장 및 정교화하기 위한 계획입니다.

---

## User Review Required

> [!IMPORTANT]
> **펌웨어 버전 상수 정의 및 Getter 추가**
> - `SmartBoxController.h` 내에 전역 펌웨어 버전 문자열 `const char* FIRMWARE_VERSION = "1.0.0";`을 정의합니다.
> - `SmartBoxController` 클래스에 `const char* getFirmwareVersion() const;`를 추가하여 웹 대시보드 API에서 이 값을 동적으로 읽을 수 있도록 합니다.

> [!IMPORTANT]
> **Web Dashboard UI 및 API 업데이트**
> - `/api/status` 엔드포인트가 반환하는 JSON 데이터에 `"version":"1.0.0"`을 추가합니다.
> - 웹 화면의 서브타이틀(`Autonomous Solar Trash Collector`) 옆에 펌웨어 버전(예: `v1.0.0`)이 실시간으로 표시되도록 DOM 바인딩 로직을 적용합니다.

---

## Proposed Changes

### [Component: SmartBox Controller]

#### [MODIFY] [SmartBoxController.h](../src/SmartBoxController.h)
- 펌웨어 버전 문자열 상수 `FIRMWARE_VERSION` 선언.
- `const char* getFirmwareVersion() const;` Getter 메서드 선언.

#### [MODIFY] [SmartBoxController.cpp](../src/SmartBoxController.cpp)
- `getFirmwareVersion()` 구현 (상수 포인터 리턴).

### [Component: Web Dashboard]

#### [MODIFY] [WebDashboard.cpp](../src/WebDashboard.cpp)
- **`/api/status` 엔드포인트 수정**: JSON 리포트에 `"version":"1.0.0"` 필드 추가.
- **`INDEX_HTML` 수정**:
  - 서브타이틀 영역 수정: `<p>Autonomous Solar Trash Collector | <span id='fwVersion'>v--</span></p>`
  - JS `updateStatus()` 내부에서 `document.getElementById('fwVersion').innerText = 'v' + data.version;` 바인딩 처리.
  - NAS 업데이트 설명 문구 정밀화: `"NAS로부터 업데이트를 다운로드 중입니다. 전원을 끄지 마세요..."`

---

## Verification Plan

### Automated Tests
- **Native 및 Board 단위 테스트**:
  - `pio test -e esp32-c6-test` 명령어를 기동하여 `test_main.cpp` 및 FSM 제어 로직에 미치는 영향이 없는지 9개 단위 테스트의 통과 상태를 재확인.

### Manual Verification
- **대시보드 접속 검증**:
  - `http://192.168.4.1`에 접속하여 헤더 밑에 `v1.0.0` 펌웨어 버전이 정상 노출되는지 확인.
- **API 응답 검증**:
  - `/api/status` 호출 시 `"version": "1.0.0"` 키-값이 정상 포함되는지 브라우저에서 확인.
