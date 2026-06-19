# HTTPS NAS 연동 OTA (Pull 방식) 및 Pre-OTA 안전 셧다운 구현 계획

본 계획서는 스마트 자동 수거함(SmartBox) 프로젝트에 대해 사용자의 내부망 NAS HTTPS 서버(`***REMOVED***`)로부터 직접 최신 펌웨어를 다운로드하여 업데이트하는 기능을 추가하고, 하드웨어의 안전을 보장하는 Pre-OTA 인터락을 통합 구현하기 위한 계획입니다.

---

## User Review Required

> [!IMPORTANT]
> **HTTPS OTA 비동기 실행 및 FreeRTOS 백그라운드 태스크 설계**
> - `httpUpdate.update()` 함수는 다운로드 및 쓰기 과정에서 전체 실행 루프를 수 초 ~ 수십 초간 동기식(Synchronous)으로 점유(블로킹)합니다.
> - 비동기 웹서버(`ESPAsyncWebServer`)의 요청 핸들러 스레드 내에서 직접 `httpUpdate.update()`를 호출하게 되면 웹서버 커넥션이 강제 종료되거나 Watchdog Timer(WDT) 락에 걸릴 위험이 있습니다.
> - 따라서 `/api/update-from-nas` GET 요청 수신 시, 즉시 클라이언트에게 "업데이트 시작" 응답(200 OK JSON)을 먼저 전송한 후, **FreeRTOS 백그라운드 태스크(Stack Size: 12KB 이상)**를 기동하여 펌웨어 다운로드 및 쓰기를 비동기적으로 수행하도록 설계합니다.

> [!IMPORTANT]
> **Pre-OTA 하드웨어 인터락 작동 보장**
> - OTA 태스크가 생성되자마자 실제 HTTPS 요청을 전송하기 전에 **`forceAllRelaysOff()`**를 호출하여 GPIO 6, 7, 8번 핀을 즉시 `INPUT`(고임피던스) 상태로 전환합니다.
> - FSM 상태를 즉각 **`STATE_OTA_UPDATING`**으로 전환하여 센서 수집 및 모터 구동 로직이 작동하는 것을 방지합니다.

---

## Open Questions

> [!NOTE]
> **`lib_ignore = WebServer` 빌드 영향 검토**
> - 현재 `platformio.ini` 설정에 `lib_ignore = WebServer`가 포함되어 있어, 아두이노 내장 WebServer 라이브러리가 제외된 상태입니다.
> - `HTTPUpdate` 라이브러리가 내장 `WebServer`에 의존성이 있는지 검토가 필요합니다. 일반적으로 `HTTPUpdate`는 `HTTPClient` 및 `WiFiClientSecure`를 사용하므로 `WebServer` 의존성은 우회할 수 있지만, 빌드 과정에서 의존성 문제가 발생하는지 빌드 및 컴파일 테스트를 통해 철저하게 검증하겠습니다.

---

## Proposed Changes

### [Component: Web Dashboard]

#### [MODIFY] [WebDashboard.cpp](../src/WebDashboard.cpp)
- **헤더 추가**: `#include <WiFiClientSecure.h>`, `#include <HTTPClient.h>`, `#include <HTTPUpdate.h>` 추가.
- **NAS OTA 태스크 함수 추가**:
  - `WiFiClientSecure` 객체를 생성하고 `client.setInsecure()`를 설정해 루트 인증서(Root CA) 검증을 생략(Synology DDNS/인증서 우회).
  - 릴레이를 선-차단(`forceAllRelaysOff`)하고 FSM 상태를 `STATE_OTA_UPDATING`으로 전환.
  - `httpUpdate.update(client, "***REMOVED***")` 호출.
  - 결과(`HTTP_UPDATE_OK`, `HTTP_UPDATE_FAILED`)에 따른 시리얼 출력 및 재부팅(`ESP.restart()`) 또는 `STATE_IDLE`로 복구 처리.
- **GET `/api/update-from-nas` 엔드포인트 등록**:
  - 요청을 받으면 클라이언트에 즉시 JSON 응답 리턴.
  - `xTaskCreate`를 통해 위의 NAS OTA 태스크를 스택 크기 `12288` (12KB)로 기동.
- **`INDEX_HTML` 웹 UI 코드 수정**:
  - 기존 OTA 카드 하단에 **"Firmware Update (from NAS)"** 영역 추가.
  - **"Fetch & Update"** 버튼 추가 및 클릭 시 `/api/update-from-nas` API 호출.
  - API 호출 즉시 화면에 `"업데이트 중입니다. 전원을 끄지 마세요..."` 경고 및 로딩 애니메이션 노출.

---

## Verification Plan

### Automated Tests
- **Native 및 Board 단위 테스트**:
  - `pio test -e esp32-c6-test` 명령어를 실행하여 기존 FSM 상태 제어, 릴레이 인터락 검증을 포함한 9개의 기존 단위 테스트가 손상되지 않았는지 회귀 테스트(Regression Test) 수행.

### Manual Verification
- **웹 대시보드 UI 확인**:
  - 브라우저를 통해 `http://192.168.4.1`에 접속하여 하단에 신규 추가된 NAS 업데이트 영역 및 Fetch & Update 버튼이 렌더링되는지 확인.
- **시리얼 모니터 로깅 검증**:
  - Fetch & Update 버튼 클릭 후 `/api/update-from-nas`가 트리거되었을 때, 시리얼 모니터 상에 릴레이 해제, `STATE_OTA_UPDATING` 진입 및 HTTPS OTA 다운로드 시작/완료/에러 처리 로그가 규칙에 맞게 정상 노출되는지 확인.
