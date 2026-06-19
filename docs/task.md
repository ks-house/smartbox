# HTTPS NAS OTA 구현 Task List

- [x] **1단계: Web Dashboard HTML UI 개편**
  - [x] `WebDashboard.cpp` 내 `INDEX_HTML`을 수정하여 "Firmware Update (from NAS)" UI 추가
  - [x] "Fetch & Update" 버튼 클릭 시 `/api/update-from-nas`를 호출하고 로딩/경고 문구를 노출하는 JS 로직 추가
- [x] **2단계: HTTPS NAS OTA 비동기 백그라운드 태스크 및 엔드포인트 구현**
  - [x] `WebDashboard.cpp`에 필요 헤더 추가 (`<WiFiClientSecure.h>`, `<HTTPClient.h>`, `<HTTPUpdate.h>`)
  - [x] `nasOtaTaskFunction` FreeRTOS 태스크 작성 (Pre-OTA 인터락, `setInsecure()`, `httpUpdate.update()` 호출 및 상태 복구/재부팅 처리)
  - [x] `/api/update-from-nas` GET 엔드포인트를 등록하여 태스크 생성 및 즉각 응답 구현
- [x] **3단계: 컴파일 빌드 및 TDD 단위 테스트 검증**
  - [x] 전체 빌드 타겟 (`esp32-c6-devkitc-1`) 빌드 검증 수행
  - [x] `pio test -e esp32-c6-test` 단위 테스트를 구동하여 회귀 검증 통과 여부 확인
- [x] **4단계: 산출물 정리 및 최종 문서 업데이트**
  - [x] 대화록 저장 스크립트 실행 및 리포트 작성
  - [x] README.md, DEVELOPMENT_MANUAL.md, USER_MANUAL.md, task.md, walkthrough.md 등 문서 업데이트 및 커밋
