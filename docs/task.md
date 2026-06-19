# 펌웨어 버전 시각화 및 HTTPS NAS OTA 구현 Task List

- [x] **1단계: SmartBoxController에 펌웨어 버전 정의 및 Getter 추가**
  - [x] `SmartBoxController.h`에 `const char* FIRMWARE_VERSION = "1.0.0";` 상수 및 `getFirmwareVersion()` 선언 추가
  - [x] `SmartBoxController.cpp`에 `getFirmwareVersion()` 구현체 작성
- [x] **2단계: Web Dashboard UI 개편 및 JSON API 업데이트**
  - [x] `WebDashboard.cpp` 내 `/api/status` API가 반환하는 JSON에 `"version": "1.0.0"` 필드 추가
  - [x] `WebDashboard.cpp` 내 `INDEX_HTML` 서브타이틀 및 JavaScript DOM 바인딩 스크립트 수정하여 버전 표시
  - [x] "Fetch & Update" 버튼 클릭 시 `"NAS로부터 업데이트를 다운로드 중입니다. 전원을 끄지 마세요..."` 피드백 노출하도록 JavaScript 수정
- [x] **3단계: 컴파일 빌드 및 TDD 단위 테스트 검증**
  - [x] 전체 빌드 타겟 (`esp32-c6-devkitc-1`) 빌드 검증 수행
  - [x] `pio test -e esp32-c6-test` 단위 테스트를 구동하여 회귀 검증 통과 여부 확인
- [x] **4단계: 산출물 정리 및 최종 문서 업데이트**
  - [x] 대화록 저장 스크립트 실행 및 리포트 작성
  - [x] README.md, DEVELOPMENT_MANUAL.md, USER_MANUAL.md, task.md, walkthrough.md 등 문서 업데이트 및 커밋
