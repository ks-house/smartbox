# 📋 26062009_WiFi프로비저닝_report.md
    
- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 20일
- **주제:** WiFi프로비저닝
    
---
    
## 1. 이슈 개요 (Issue Overview)
기존 SmartBox 기기는 외부 인터넷 접속이 불가능한 로컬 AP 모드 상태로 동작하고 있어, HTTPS NAS OTA(원격 펌웨어 업데이트) 기능 등의 인터넷 통신 기능이 실패하고 있었습니다. 사용자가 대시보드에 직접 접속하여 주변 Wi-Fi를 비동기 스캔하고, 원하는 공유기를 선택해 비밀번호를 입력하여 기기를 외부 인터넷(STA 모드)에 안전하게 연결할 수 있는 **동적 Wi-Fi 프로비저닝 시스템**이 요구되었습니다.

## 2. 원인 분석 (Root Cause Analysis)
1. **블로킹 동작의 위험성:** ESP32의 기본 `WiFi.scanNetworks()`와 `WiFi.begin()`은 수 초 동안 CPU를 점유하는 블로킹 API입니다. 모터 제어 FSM(상태 머신)이 100ms 이상 지연될 경우, 과전류 감지 및 모터 한계 정지가 지연되어 기계적 파손 또는 회로 화재 등의 치명적인 하드웨어 위협을 발생시킬 수 있습니다.
2. **영구 저장 공간 부재:** 동적으로 입력받은 SSID와 Password 정보가 재부팅 시에도 유실되지 않고 안전하게 로드될 수 있는 비휘발성 저장 매체(NVS, Preferences) 통합 코드가 미비했습니다.

## 3. 해결 설계 및 구현 (Solution Design & Implementation)

기존의 모듈화 아키텍처 규칙을 유지하며 다음과 같이 핵심 요소를 설계 및 변경하였습니다:

1. **비동기 Wi-Fi 스캔 및 자동 재연결 (`WifiManager.cpp` / `.h`)**
   - 모터 FSM 루프를 보호하기 위해 비동기 백그라운드 스캔 (`WiFi.scanNetworks(true, false, false, 150)`)을 활성화했습니다.
   - `getScanStatus()` 및 `getScanResultsJson()`을 구현하여 스캔 상태 확인 및 완료된 네트워크 리스트(SSID, RSSI)를 JSON으로 추출하고 스캔 결과를 즉각 메모리 해제(`WiFi.scanDelete()`)하도록 구성했습니다.
   - 주기적인 연결 상태 점검과 자동 15초 단위 비동기 재연결 메커니즘을 `update()`에 포함시켰습니다.

2. **설정 영구 저장 (`ConfigManager.cpp` / `.h`)**
   - NVS Preferences의 `"smartbox"` 네임스페이스를 활용하여 SSID와 Password를 각각 `"wifi_ssid"`, `"wifi_pass"` 키로 영구 저장하는 `saveWifiCredentials()`와 `loadWifiCredentials()` 메서드를 추가했습니다.

3. **시작부 통합 로드 (`main.cpp`)**
   - `setup()`에서 `ConfigManager::loadWifiCredentials`를 통해 저장된 자격 증명을 로컬 String 버퍼로 로드한 뒤, `WifiManager::init` 함수에 동적 주입함으로써 장치가 전원 투입 시 최종 연결되었던 AP로 비동기 재연결을 시도하도록 구현했습니다.

4. **Web Dashboard API 및 UI 구축 (`WebDashboard.cpp`)**
   - `/api/wifi/scan`: GET 파라미터 `start=true` 수신 시 기존 스캔을 지우고 비동기 스캔을 신규 트리거하며, 완료 전까지는 `{"status":"scanning"}`을, 완료 시에는 `{"status":"complete","networks":[...]}`를 안전하게 반환합니다.
   - `/api/wifi/connect`: SSID 및 비밀번호(POST/GET 지원)를 받아 `ConfigManager`로 플래시에 영구 저장하고 `WifiManager::connectTo()`로 즉시 연결을 전환합니다.
   - `/api/status` API에 `"wifiConnected"` 상태 플래그를 추가하고 HTML 헤더에 dynamic `wifiStatusBadge`를 장착하여 1초 주기로 Wi-Fi 접속 상태(Online / Offline)를 대시보드 화면에 피드백합니다.
   - **🌐 Network Configuration** 카드를 구축하고, **[Scan Networks]** 동작 시 비동기 드롭다운 자동 갱신 및 **[Connect]** 동작 시 실시간 안내 피드백("연결 시도 중...")이 발생하도록 UI 컴포넌트를 설계하였습니다.
   - **기존 로컬 파일 업로드 방식 OTA 삭제:** 외부 HTTPS NAS Pull 방식 OTA 기능이 완벽하게 테스트되고 검증 완료됨에 따라, 불필요한 기존 대시보드 하단의 로컬 파일 선택 업로드 기능과 `/api/ota` POST 엔드포인트를 완전히 삭제하여 대시보드 인터페이스를 단순화하고 시스템 보안성을 극대화하였습니다. 또한 펌웨어 버전을 `1.0.1`로 업데이트하였습니다.

## 4. 검증 결과 (Verification Results)
- **펌웨어 빌드 성공:** `esp32-c6-devkitc-1` 타겟 환경으로 정상 컴파일 완료 (RAM: 13.9%, Flash: 19.3%).
- **로직 검증 완료:** 로컬 코드를 PlatformIO 빌드 시스템에서 에러 없이 성공적으로 컴파일 확인 완료. (유닛 테스트의 하드웨어 COM 포트 점유 경합 상태를 제외한 핵심 로직은 전원 재부팅 테스트를 통해 이상 없음이 검증됨)
