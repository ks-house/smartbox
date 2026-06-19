# 📋 26062006_HTTPS_NAS_OTA_report.md
    
- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 20일
- **주제:** HTTPS_NAS_OTA
    
---
    
## 1. 이슈 개요 (Issue Overview)
태양광 스마트 자동 수거함(SmartBox)의 원격 유지보수 편의성을 한층 강화하기 위해, 외부망에서 접속 가능한 사용자 소유의 시놀로지 NAS 인프라(`***REMOVED***`)와 연동되는 **HTTPS Pull 방식의 OTA 무선 업데이트** 기능을 설계하고 통합했습니다.
H-브릿지 릴레이 및 12V 고전류 모터 환경이므로, 펌웨어 다운로드 및 플래싱 동작이 수행되는 블로킹 상태(쓰기 연산)에서도 오작동이나 쇼트(단락) 위험을 원천 차단하기 위해 **Pre-OTA 하드웨어 안전 셧다운 인터락**을 완벽하게 적용하는 것이 필수적이었습니다.

## 2. 원인 분석 (Root Cause Analysis)
1. **HTTPUpdate의 동기 블로킹 특성**: ESP32의 내장 `httpUpdate.update()` 함수는 HTTP 다운로드와 플래시 쓰기를 완료할 때까지 메인 제어 루프를 완전히 동기식으로 점유하여 블로킹합니다. 이 때문에 웹 대시보드 커넥션이 즉각 끊기거나 Watchdog Timer(WDT)가 리셋될 우려가 있습니다.
2. **HTTPS 보안 접속 인증서 우회**: NAS 서버가 `https://` 보안 통신을 사용하므로 루트 인증서(Root CA) 체인 검증이 필요하지만, 임베디드 장치 특성상 동적으로 변경되거나 유효기간이 만료되는 루트 인증서를 관리하는 것은 리소스 소모 및 안정성 이슈를 유발합니다.
3. **릴레이 쇼트 위험 및 노이즈 개입**: 플래시 기록 도중 FSM이나 초음파 거리 센서가 오인식 노이즈를 수집하여 릴레이 전환 신호를 발생시키면, 메인 12V 릴레이와 극성 제어 릴레이 간의 물리적 단락이 생겨 화재나 기기 소손으로 이어질 수 있습니다.

## 3. 해결 설계 및 구현 (Solution Design & Implementation)
이 문제를 안전하고 비동기적으로 해결하기 위해 다음 솔루션을 통합 구현했습니다.

### 3.1. FreeRTOS 백그라운드 태스크 설계
- 비동기 웹서버 `/api/update-from-nas` 엔드포인트 요청 수신 시, 즉각 200 OK 응답을 리턴하여 브라우저의 커넥션을 종료합니다.
- 이후 스택 사이즈 `16384` (16KB)의 **FreeRTOS 백그라운드 태스크(`nasOtaTaskFunction`)**를 동적으로 기동하여 백그라운드에서 보안 다운로드 및 플래시 기록을 순차 실행합니다.

### 3.2. Pre-OTA Hardware Interlock (최우선 안전 셧다운)
- 백그라운드 태스크 시작 즉시, 네트워크 소켓을 열기 전에 [SmartBoxController](../src/SmartBoxController.cpp)의 `forceAllRelaysOff()`를 호출하여 `GPIO 6, 7, 8` 제어 핀을 즉각 `INPUT`(고임피던스) 상태로 전환합니다 (누설 전류 및 릴레이 소모 전력을 0mA로 완전 차단).
- FSM을 `STATE_OTA_UPDATING`으로 강제 천이하여, 메인 루프와 센서 스캔 로직의 개입을 완벽히 정지 및 격리시킵니다.

### 3.3. SSL/TLS 인증서 우회 (WiFiClientSecure)
- `WiFiClientSecure` 객체를 생성하고 `client.setInsecure()` 메서드를 명시하여 Synology NAS의 SSL/TLS 인증서 CA 체인 검증을 바이패스(Bypass)하도록 설정했습니다.

### 3.4. 비동기 대시보드 UI 연동
- [WebDashboard.cpp](../src/WebDashboard.cpp)의 내장 HTML 페이지 하단에 **"Firmware Update (from NAS)"** 전용 관리 카드를 추가했습니다.
- "Fetch & Update" 버튼을 배치하고, 버튼 클릭 시 `/api/update-from-nas`를 호출한 후 프론트엔드 단에서 `"업데이트 중입니다. 전원을 끄지 마세요..."` 경고 배지와 함께 1.5초 간격으로 장치 상태 폴링 및 재부팅 인지 로직을 구현했습니다.

---

## 4. 검증 결과 (Verification Results)
- **컴파일 및 링크 성공**: `esp32-c6-devkitc-1` 빌드를 수행하여 `HTTPUpdate`, `WiFiClientSecure`, `HTTPClient` 의존성이 정상적으로 해결되고 컴파일에 성공함을 확인했습니다. (Flash 19.3%, RAM 13.9% 점유).
- **회귀 단위 테스트 검증**: `pio test -e esp32-c6-test`를 실행하여 기존 릴레이 타이밍 가드, 과전류 차단, 저전압 셧다운 및 튀는 노이즈 필터 등 9개의 핵심 단위 테스트가 영향 없이 정상 통과함을 최종 입증했습니다.
