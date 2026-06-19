# 📋 26062007_HTTPS_OTA_버전표시_report.md
    
- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 20일
- **주제:** HTTPS_OTA_버전표시
    
---
    
## 1. 이슈 개요 (Issue Overview)
스마트 자동 수거함(SmartBox)의 배포본 관리 및 추적성을 향상하기 위해, 현재 구동 중인 **펌웨어 버전 문자열을 정의 및 획득**하는 구조를 구축하고, 이를 웹 대시보드 화면상에 실시간 시각화하여 사용자가 직관적으로 확인하게끔 처리했습니다. 또한, NAS 연동 HTTPS OTA 시작 시 사용자에게 더 정확한 알림 피드백을 노출하도록 고도화하였습니다.

## 2. 원인 분석 (Root Cause Analysis)
1. **배포 버전 추적성 부재**: 기기 기동 및 OTA 업데이트 이후, 현재 수거함에 정상적으로 타겟 버전의 바이너리가 적용되었는지 기기 수준이나 대시보드 상에서 확인할 수 있는 경로가 부재했습니다.
2. **FSM 구조와의 긴밀한 결합**: 버전 정보를 단순히 웹서버 파일에 하드코딩하기보다, FSM 제어 루프를 담당하는 `SmartBoxController` 내부의 정적 멤버나 상수를 통해 API로 안전하게 호출하는 것이 추상화 설계 철학에 부합합니다.
3. **직관적인 피드백 문구 필요**: NAS OTA의 다운로드 작업 시작 시 사용자에게 보다 명확하게 `"NAS로부터 업데이트를 다운로드 중입니다. 전원을 끄지 마세요..."` 라는 피드백을 전달하여 임의 전원 차단을 방지할 필요가 있었습니다.

## 3. 해결 설계 및 구현 (Solution Design & Implementation)
이 문제를 정교하게 해결하기 위해 기존 모듈을 보강하여 구현했습니다.

### 3.1. 전역 펌웨어 버전 상수 및 Getter 확보
- [SmartBoxController.h](../src/SmartBoxController.h)에 클래스 static constexpr 멤버인 `FIRMWARE_VERSION`을 `"1.0.0"`으로 정의했습니다.
- `const char* getFirmwareVersion() const` 메서드를 구현하여 웹 서버 등 외부에서 안전하게 접근 가능한 경로를 마련했습니다.

### 3.2. JSON API 및 대시보드 DOM 바인딩 업데이트
- [WebDashboard.cpp](../src/WebDashboard.cpp)의 `/api/status` API가 반환하는 JSON에 `"version": "1.0.0"` 필드를 추가했습니다:
  ```cpp
  json += "\"version\":\"" + String(controllerPtr->getFirmwareVersion()) + "\",";
  ```
- 웹 대시보드 HTML 타이틀 영역 하단에 `<span id='fwVersion'>v--</span>` DOM 요소를 배치하였습니다.
- JS `updateStatus` 함수 내부에서 주기적(/api/status 폴링)으로 버전 정보를 파싱하여 `document.getElementById('fwVersion').innerText = 'v' + data.version;`을 바인딩 처리했습니다.

### 3.3. NAS OTA 로딩 피드백 문구 개선
- Fetch & Update 버튼 클릭 시 API 호출 대기 및 로딩 중 경고 배지에 정확히 **`"NAS로부터 업데이트를 다운로드 중입니다. 전원을 끄지 마세요..."`**가 출력되도록 수정했습니다.
- 백그라운드 OTA 플래싱 모드(`STATE_OTA_UPDATING`)에 진입 시에도 `"(플래싱 중)"` 문구를 덧붙여 노출시켰습니다.

---

## 4. 검증 결과 (Verification Results)
- **타겟 보드 빌드 및 사이즈 검증**: `esp32-c6-devkitc-1` 컴파일을 수행하여 빌드에 성공했습니다. 추가 리소스 사용은 거의 없습니다.
- **TDD 단위 테스트 결과**: `pio test -e esp32-c6-test` 명령어를 실행하여 9개의 유닛 테스트 케이스가 아무런 부작용 없이 전원 정상 통과(PASSED)됨을 입증했습니다.
