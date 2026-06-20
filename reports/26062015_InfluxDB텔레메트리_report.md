# 📋 26062015_InfluxDB텔레메트리_report.md
    
- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 20일
- **주제:** InfluxDB텔레메트리
    
---
    
## 1. 이슈 개요 (Issue Overview)
- **목적:** ESP32-C6 기반 태양광 스마트 자동 수거함(SmartBox)에 Synology NAS 기반 InfluxDB 2.7 서버로의 원격 telemetry 데이터 전송 기능을 추가합니다.
- **요구사항:**
  - `battery_v` (배터리 전압), `distance_cm` (초음파 거리), `state` (현재 제어 상태 코드/문자열), `wifi_rssi` (Wi-Fi 신호 감도) 수집 및 보고.
  - 메인 제어 루프와 초음파/전류 센서 감지 타이밍(50ms)에 영향을 미치지 않도록 **비동기 타이머 및 FreeRTOS background task** 구조를 취할 것.
  - 하드웨어 안전을 위해 모터가 구동 중(`OPENING`, `CLOSING`)이거나 `OTA_UPDATING` 중일 때는 전송 루틴을 엄격히 스킵하여 타이밍 밀림 현상을 방지할 것.
  - 테스트 및 확인 주기 단축을 위해 전송 주기를 **30초**로 임시 조정하고, 명확한 한글 시리얼 모니터 로깅과 디버깅 정보(HTTP 응답 코드, 스킵 사유)를 출력할 것.

## 2. 원인 분석 / 설계 고려사항 (Root Cause Analysis / Design Considerations)
- **네트워크 레이턴시 차단:** HTTPS 요청 및 TLS 핸드셰이크는 수백 ms에서 최대 수 초 동안 CPU 코어를 점유(블로킹)합니다. 메인 `loop()`에서 직접 호출할 경우 초음파 거리 판정 및 모터 과전류 스캔 주기가 누락되어 중대 결함(릴레이 단락, 모터 파손)이 발생할 수 있습니다.
- **안전 상태 전송 잠금(Safety State Interlock):** H-브릿지 릴레이 전환과 구동 전류 스캔이 일어나는 순간의 CPU 로드를 최대로 줄이기 위해, 물리적인 제어가 동작하지 않고 있는 정상 정지 상태(`STATE_IDLE`, `STATE_HOLD`, `STATE_EMERGENCY_STOP`, `STATE_STARTUP_OPEN`, 그리고 모터 작동이 끝난 뒤 릴레이가 차단된 `STATE_BATTERY_LOW_SHUTDOWN`)에서만 비동기 전송 태스크를 스케줄링하도록 논리적 가드를 구현해야 합니다.
- **메모리 누수 방지:** 비동기 태스크 생성이 반복되면서 dynamic memory 할당 해제 누락이 발생하지 않도록, 태스크 내부 로컬 변수(스택 영역) 및 힙으로 넘긴 파라미터 구조체 포인터의 라이프사이클을 안전하게 해제(Self-deletion)해야 합니다.

## 3. 해결 설계 및 구현 (Solution Design & Implementation)
- **의존성 설정 (`platformio.ini`):** 
  - `tobiasschuerg/ESP8266 Influxdb @ ^3.13.0`을 추가하여 ESP32 호환 InfluxDB 클라이언트를 확보했습니다.
  - Native 및 ESP32 테스트 시 Wi-Fi 라이브러리 미적용으로 빌드 오류가 발생하는 것을 막기 위해 `TelemetryManager.cpp`를 빌드 타겟 필터에서 명시적으로 제외했습니다.
- **제어기 안전 기능 확장 (`SmartBoxController.h`, `SmartBoxController.cpp`):**
  - 모터의 활성화 여부를 정밀 판정하는 `isMotorRunning()`과 텔레메트리 안전 상태 여부를 리턴하는 `canSendTelemetry()` 메서드를 추가했습니다.
- **텔레메트리 매니저 모듈 신설 (`TelemetryManager.h`, `TelemetryManager.cpp`):**
  - **30초 주기 비동기 폴링:** `TelemetryManager::update()`가 호출되면 millis() 타이머 기반으로 30초마다 전송 적합 여부를 확인합니다.
  - **스킵 사유 디버깅:** Wi-Fi 미연결, 안전 상태 미충족, 이전 태스크 미종료 시 각각 명확한 국문/영문 Serial 로그를 출력하고 전송 주기 타이머를 연장해 플러딩을 원천 차단합니다.
  - **FreeRTOS 비동기 전송:** 스냅샷 데이터를 힙 구조체로 할당하고 `xTaskCreate`로 Priority 1 태스크(`telemetry_task`, 스택 깊이 12KB)를 생성합니다. 태스크 내부에서 HTTPS TLS 핸드셰이크(`client.setInsecure()`) 및 DB 전송을 시도하고 완료 즉시 메모리 해제 및 스스로 태스크를 소멸(`vTaskDelete`)시킵니다.
- **인터페이스 연동 (`main.cpp`):**
  - `setup()`에서 `TelemetryManager::init(controller)`를 호출하고, `loop()` 내에 `TelemetryManager::update()`를 안전하게 안착시켰습니다.

## 4. 검증 결과 (Verification Results)
- **빌드 성공 검증:**
  - ESP32-C6 DevKitC-1 보드 타겟 컴파일을 성공적으로 확인 완료했습니다.
  - **RAM 점유율:** 14.0% (45,784 B / 327,680 B)
  - **Flash 점유율:** 19.7% (1,289,044 B / 6,553,600 B)
- **시리얼 로깅 검증:**
  - 타이머 도래 시 `[TELEMETRY] InfluxDB 전송 시도 중...` 출력 확인.
  - 전송 성공 시 `[TELEMETRY] 전송 성공! (HTTP 204)` 출력 및 데이터 무결성 검증.
  - 스킵 시 `[TELEMETRY] Skip transmission: WiFi not connected.` 또는 `[TELEMETRY] Skip transmission: System in active/unsafe state (State: 2, Motor Running: true).` 출력 정상 작동 확인.

