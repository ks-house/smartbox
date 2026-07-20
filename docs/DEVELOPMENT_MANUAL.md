# SmartBox 조립 및 개발 매뉴얼 (최신 아키텍처 개편본)

> **문서 버전:** 최신 커밋 기준 (2026-07-20)  
> **펌웨어 기준 브랜치:** main (dbcfff6)

본 문서는 ESP32-C6 기반 스마트 자동 수거함 시스템의 하드웨어 스펙, 물리 결선 가이드, 안전 설계 규칙 및 소프트웨어 논블로킹 상태 머신(FSM), MQTT 기반 모던 IoT 통합 아키텍처 사양을 규정하는 개발자 공식 가이드라인입니다.

---

## 1. 확정 부품 정보 (Hardware BOM)

* **수거함 외장재:**
  * 제품명: 레드아이 야외보관함 290L (무게 8.5kg)
  * 소재: PP (폴리프로필렌)
  * 실측 규격: 가로 110cm x 세로 46cm x 높이 52.5cm

* **제어부:**
  * MCU: ESP32-C6-DevKitC-1 (16MB Flash, 아두이노 3.x 환경, pioarduino 포크)

* **센서부:**
  * 거리 감지: AJ-SR04T 방수 초음파 센서 모듈
  * 전력 모니터: INA219 (MCU-219) I2C 직류 전류/전압 센서 모듈

* **구동 및 전원부:**
  * 액추에이터: 12V 100N 200mm 스트로크 리니어 액추에이터 x 2개 (병렬 구동)
  * 1채널 릴레이: 5V 1채널 릴레이 모듈 (메인 12V 전원 컷오프용 / Low-Level Trigger)
  * 2채널 릴레이: 5V 2채널 릴레이 모듈 (모터 정역회전 극성 제어용 / Low-Level Trigger)
  * 강압 컨버터: LM2596 DC-DC 강압 컨버터 모듈 (12V -> 5V 제어 전원용)
  * 태양광 충전: Eco-worthy 30W 태양광 패널, 10A PWM 충전 컨트롤러
  * 배터리: 12V 7Ah 밀폐형 연납축전지 (메인 에너지원)
  * 안전 스위치: 12V 고용량 방수 토글스위치 (메인 제어 전원 차단용)

* **접근성 부자재:**
  * IP65 방수 하이박스(최소 200x300x130mm), 케이블 글랜드, PP 판재 파손 방지용 광폭 와셔 또는 고하중 덧댐 보강판(3T 이상)

---

## 2. 기구부 정밀 장착 가이드 (레드아이 290L 최적화)

PP(폴리프로필렌) 소재 특성상 나사 결합 부위에 과도한 응력이 집중되면 박스가 찢어질 수 있으므로 반드시 아래 보강 조치를 수반해야 합니다.

1. **상단 브라켓 설치 (뚜껑부):**
  * 뚜껑을 완전히 닫은 상태에서 뚜껑 안쪽 면, 뒤쪽 경첩(힌지) 중심으로부터 앞쪽으로 **35.4cm** 떨어진 위치에 클레비스 브라켓을 고정합니다.
  * **파손 방지 팁:** 볼트 체결 시, 뚜껑 바깥쪽에 넓은 광폭 와셔나 덧댐 판을 대고 체결하여 액추에이터가 밀어 올릴 때의 하중을 분산시킵니다.

2. **하단 브라켓 설치 (본체 내부 측면):**
  * 상자 내부 양쪽 벽면, 경첩 중심에서 앞쪽으로 **9.4cm**, 위쪽 테두리에서 아래쪽으로 **15.0cm** 내려온 위치에 고하중 ㄱ자 꺾쇠를 고정합니다.
  * PP 벽면 외부에 두꺼운 보강판을 대고 볼트-너트로 본체 벽을 안팎에서 샌드위치처럼 맞물려 조립해야 합니다.

---

## 3. 제어부 및 센서 정밀 핀 맵핑 (Pin Mapping)

ESP32-C6 보드와 각 모듈은 아래의 확정된 GPIO 번호에 정밀하게 결선되어야 합니다.

| 모듈 명칭 | 모듈 핀 | ESP32-C6 GPIO | 핀 제어 특성 및 역할 |
| --- | --- | --- | --- |
| **방수 초음파 센서** | **TRIG** | **GPIO 4** | 트리거 신호 출력 |
| **방수 초음파 센서** | **ECHO** | **GPIO 5** | 에코 신호 입력 |
| **1채널 릴레이 (Main)** | **IN** | **GPIO 6** | Low-Level Trigger (메인 12V 전원 공급/차단) |
| **2채널 릴레이 (IN1)** | **IN1** | **GPIO 7** | Low-Level Trigger (액추에이터 정회전 / 개방) |
| **2채널 릴레이 (IN2)** | **IN2** | **GPIO 8** | Low-Level Trigger (액추에이터 역회전 / 폐쇄) |
| **INA219 전류 센서** | **SDA** | **GPIO 22** | I2C 데이터 라인 |
| **INA219 전류 센서** | **SCL** | **GPIO 23** | I2C 클럭 라인 |

---

## 4. 전원 스위치 및 릴레이 물리 결선 가이드 (상시 감시 개선 모델)

배터리 전압의 상시 모니터링을 위해, INA219 모듈을 1채널 메인 릴레이의 상류(전원 인입부)에 직결하는 개선 구조를 채택합니다.

### Step 1. 상시 충전 루프 연결
1. 태양광 패널의 (+), (-) 선 -> 충전 컨트롤러 Solar 단자
2. 12V 7Ah 배터리의 (+), (-) 단자 -> 충전 컨트롤러 Battery 단자 (스위치 OFF 시에도 상시 충전 유지)

### Step 2. 메인 스위치 및 제어 전원 분기
1. **플러스(+) 메인 라인:** 12V 배터리 (+) -> 방수 토글스위치 -> Y자 분기 -> [분기 A: LM2596 IN+], [분기 B: INA219 Vin+]
2. **마이너스(-) 공통 그라운드:** 12V 배터리 (-) -> LM2596 IN- 및 2채널 릴레이 NC 단자 직결

### Step 3. 제어부 및 구동부 종단 결선
1. **LM2596 출력단:** 토글스위치 ON 상태에서 정확히 **5.0V** 맞춘 후 ESP32-C6의 5V 및 GND 핀에 인가.
2. **H-브릿지 및 전류 센서 경로:**
   * INA219 Vin- -> 1채널 릴레이 COM
   * 1채널 릴레이 NO -> 2채널 릴레이 두 NO 단자 공통 점퍼
   * 2채널 릴레이 COM1, COM2 단자에서 두 액추에이터 모터선으로 병렬 분기 결선

---

## 5. 소프트웨어 핵심 알고리즘 및 시스템 통신 단일화

### 5.1. 소스 코드 모듈 구조

| 파일 | 역할 |
| :--- | :--- |
| main.cpp | 진입점. 컴포넌트 초기화 및 FreeRTOS NetworkTask 생성 |
| SmartBoxController.cpp/.h | 핵심 FSM 엔진. 11개 상태 전이, 릴레이 인터락, 센서 읽기 |
| WifiManager.cpp/.h | Wi-Fi AP+STA 듀얼 모드, Captive Portal, 5분 온디맨드 프로비저닝 |
| MqttManager.cpp/.h | MQTTS over TLS, Home Assistant Auto Discovery, 원격 명령 처리 |
| AutoOtaManager.cpp/.h | HTTPS NAS 기반 OTA. 1시간 주기 자동 버전 확인 및 업데이트 |
| TelemetryManager.cpp/.h | 배치 텔레메트리. 모터 구동 중 500ms 샘플링 + 1분 주기 heartbeat |
| PowerManager.cpp/.h | NTP 기반 매일 04:00 AM 자동 안전 재부팅 |
| ConfigManager.cpp/.h | Preferences NVS 기반 설정/Wi-Fi/뚜껑상태 영구 저장 |
| RemoteLogger.cpp/.h | WARN/ERROR 이상 로그 MQTT 자동 전달 |
| Esp32Hardware.cpp/.h | HardwareInterface 구현체 (INA219, 초음파, GPIO) |

### 5.2. FreeRTOS 태스크 구조

시스템은 2개의 FreeRTOS 태스크로 분리 운영됩니다:

| 태스크 | 우선순위 | 스택 | 담당 기능 |
| :--- | :--- | :--- | :--- |
| **loopTask** (Arduino loop()) | 기본 | 기본 | FSM 업데이트, 센서 읽기, Wi-Fi, PowerManager |
| **NetworkTask** | 1 (Low) | 16KB | AutoOTA, TelemetryManager, MqttManager, WDT 피드 |

* **NetworkTask**는 16KB 스택을 할당받아 TLS 핸드셰이크 및 HTTPUpdate 중 스택 오버플로우를 방지합니다.
* 두 태스크 모두 Task Watchdog Timer(TWDT) 10초 타임아웃에 등록됩니다. OTA 플래싱 중에는 NetworkTask가 WDT에서 해제됩니다.

### 5.3. 유한 상태 머신 (11-States FSM) 정의

`
                   (Boot)
                       |  initRelays + loadLidState
                       v
                    IDLE  <-----------------------------+
                       |  Human < distThreshold (300ms)  |
                       v                               |
                 OPEN_START                            |
                       |  setRelayStates(Main ON, Dir-A ON)  |
                       v                               |
                   OPENING  ----[stall x3]--> EMERGENCY_STOP
                       |  3.8s timeout                |
                       v                               |
                    HOLD                               |
                       |  waitTime expired (10s)       |
                       v                               |
                CLOSE_START                            |
                       |  setRelayStates(Main ON, Dir-B ON)  |
                       v                               |
                  CLOSING  ---[3s blind time]--> IDLE -+
                       |  [reopen] if human detected
                       v
                 OPEN_START (reopen path)
`

추가 상태:
* **EMERGENCY_STOP:** 과전류 3회 연속 -> 5초 자동 복구 또는 reset_emergency 명령
* **BATTERY_LOW_SHUTDOWN:** 11.3V 이하 3초 -> 강제 개방 -> 릴레이 영구 격리
* **OTA_UPDATING:** 펌웨어 업데이트 중 -> 전 릴레이 차단, FSM 동결
* **MAINTENANCE:** 5분 정비 모드 -> 자동 닫힘
* **STARTUP_OPEN:** NVS에서 열림 상태 복원 -> CLOSE_START 홈밍

### 5.4. 주요 설계 결정사항

#### 통합 상태 변경 콜백 (BUG-F 수정)

main.cpp의 
egisterStateCallback() 람다는 NVS 저장과 MQTT 이벤트 발행을 단일 콜백으로 통합합니다. 이전에 두 콜백을 분리 등록하면 두 번째 등록이 첫 번째를 덮어쓰는 버그가 있었습니다.

#### 중앙 mutex 보호 (dataMutex)

SmartBoxController는 std::recursive_mutex dataMutex로 배터리 전압, 모터 전류, 거리, FSM 상태 등 모든 공유 데이터를 보호합니다. FreeRTOS loopTask와 NetworkTask 간 경합을 방지합니다.

#### 릴레이 방향 캐시 (lastDirOpen/lastDirClose)

이전에는 setRelayStates()의 static local 변수로 관리하여 테스트 간 오염이 발생했습니다. 현재는 클래스 멤버 변수로 이관하여 테스트 격리가 완전히 보장됩니다 (커밋 dbcfff6).

#### 배치 텔레메트리 (TelemetryManager)

* **모터 구동 중 (eventBuffer):** 500ms 간격으로 최대 20포인트 샘플링. 구동 완료 후 batch 타입으로 MQTT 발행.
* **상시 heartbeat (heartbeatBuffer):** 1분 간격으로 최대 60포인트 샘플링. configurable 주기(기본 10분)로 heartbeat 타입으로 MQTT 발행.

---

## 6. 임베디드 개발 필수 안전 규칙 (Critical Coding Rules)

> **[규칙 1] Low-Level Trigger 부팅 글리치 차단**
> ESP32-C6가 재부팅되는 초기 시점에 GPIO 6, 7, 8번 핀의 전위 요동으로 릴레이가 오구동될 수 있습니다. setup() 초기에 INPUT_PULLUP 모드로 핀을 먼저 HIGH로 풀업한 후 OUTPUT으로 전환하는 순서를 반드시 지키십시오.

> **[규칙 2] H-브릿지 단락(쇼트) 방지 소프트웨어 인터락**
> GPIO 7(IN1)과 GPIO 8(IN2)이 동시에 LOW(ON)로 인가될 경우 12V 배터리 단자가 쇼트되어 화재가 발생합니다. setRelayStates()의 인터락 가드(dirOpen && dirClose 동시 true 차단)를 절대 제거하지 마십시오.

> **[규칙 3] 고임피던스(High-Impedance) 릴레이 차단 기법**
> ESP32의 3.3V Logic Level과 5V Active-Low 릴레이 간 전압 차이로, GPIO를 HIGH로만 출력하면 포토커플러 내부 LED가 완전히 꺼지지 않습니다. 릴레이 차단 시 반드시 pinMode(PIN, INPUT) 고임피던스 모드를 사용하여 대기 전력을 0mA로 리셋하십시오.

> **[규칙 4] OTA 중 저전압 가드 우회**
> TLS 핸드셰이크 중 순간 전압 강하가 발생하면 배터리 방전으로 오인하여 BATTERY_LOW_SHUTDOWN이 트리거될 수 있습니다. AutoOtaManager::isOtaInProgress() 플래그 확인을 통해 OTA 구간에서 저전압 감지 루틴을 우회합니다.

---

## 7. MQTT 단일 파이프라인 & Home Assistant Auto Discovery

### 7.1. 토픽 구조

| 토픽 | QoS | Retain | 설명 |
| :--- | :--- | :--- | :--- |
| smartbox/status | 1 | Y | 온라인/오프라인 상태 (LWT) |
| smartbox/command | 1 | N | 원격 명령 수신 (force_open, reboot, trigger_ota 등) |
| smartbox/event/state | 1 | N | FSM 상태 전이 이벤트 |
| smartbox/event/motion | 0 | N | 모션 감지 이벤트 |
| smartbox/event/alarm | 1 | N | 비상 정지/저전압 알람 |
| smartbox/event/cycle | 0 | N | 개폐 사이클 완료 요약 |
| smartbox/telemetry | 0 | N | 30초 주기 실시간 텔레메트리 |
| smartbox/telemetry/batch | 0 | N | 배치 텔레메트리 (batch/heartbeat/wakeup) |
| smartbox/config/state | 1 | Y | 현재 설정값 상태 (dist, wait, stall 등) |
| smartbox/log | 0 | N | WARN/ERROR 수준 원격 로그 |
| homeassistant/\*/smartbox_01/\*/config | 1 | Y | HA Auto Discovery 설정 패킷 |

### 7.2. Home Assistant Auto Discovery 엔티티 (총 16개)

* **Buttons (7개):** force_open, force_close, reset_emergency, trigger_ota, reboot, debug_on, maint_start
* **Sensors (6개):** state, battery_v, cycle_duration, peak_current, distance, wifi_rssi
* **Binary Sensors (2개):** status (connectivity), motion
* **Selects (5개):** config_dist, config_wait, config_stall, config_ota_hour, config_tel_int

---

## 8. GitHub Actions CI/CD 및 Native 테스트 환경

### 8.1. PlatformIO 빌드 환경

| 환경 | 플랫폼 | 설명 |
| :--- | :--- | :--- |
| esp32-c6-devkitc-1 | pioarduino/platform-espressif32 | 메인 프로덕션 빌드 |
| esp32-c6-test | pioarduino/platform-espressif32 | 통합 테스트 빌드 (TARGET_TEST 매크로) |
| native | native (g++) | FSM 로직 단위 테스트 (NATIVE_BUILD 매크로) |

**주요 라이브러리:**
* Adafruit INA219 @ ^1.2.3 - 배터리/전류 센서
* PsychicMqttClient @ ^0.2.1 - async MQTT over TLS
* ArduinoJson @ ^7.0.4 - JSON 직렬화
* AsyncTCP - PsychicMqttClient 의존성

**파티션:** default_16MB.csv (16MB Flash 활용)

### 8.2. CI/CD 자동 배포 흐름

1. **CI - Test:** main 브랜치 푸시 시 Ubuntu에서 pio test -e native 실행
2. **CI - Build:** 테스트 통과 시 pio run -e esp32-c6-devkitc-1 실행
3. **CD - Deploy:** firmware.bin 및 version.json을 Synology NAS의 지정 디렉토리로 SFTP 안전 배포

---

## 9. 보안 및 민감 정보 관리 (Secrets)

민감 정보는 include/secrets.h에 집중 관리하며, 해당 파일은 .gitignore로 Git에서 제외됩니다.

| 매크로 | 설명 |
| :--- | :--- |
| SECRET_AP_PASS | SmartBox-WiFi AP 비밀번호 |
| SECRET_MQTT_HOST | MQTT 브로커 호스트명 |
| SECRET_MQTT_PORT | MQTT 브로커 포트 (기본 4883) |
| SECRET_MQTT_USER / PASS | MQTT 인증 자격증명 |
| SECRET_ROOT_CA_CERT | TLS 루트 CA 인증서 (PEM 형식) |

더미 값이 적힌 include/secrets.h.example을 참고하여 설정하십시오.

---

## 10. 추가 개선 사항 (TODO-LIST)

- [ ] **[FEAT-1] set_config 동적 제어 매개변수 확장:** cooldownTime, maxHoldTime, emergencyRecoveryTime 값을 MQTT로 동적 변경하도록 handleSetConfig() 확장이 필요합니다.
- [ ] **[FEAT-4] 다중 콜백 등록 구조 개선:** SmartBoxController의 상태 변경 콜백을 std::vector<StateChangeCallback> 기반 콜백 체인으로 리팩토링 필요.
- [ ] **[FEAT-5] 상태 정보 조회 명령 (get_info) 추가:** 펌웨어 버전, Wi-Fi RSSI, OTA 상태, 구동 시간 등을 MQTT 명령으로 수동 조회하는 기능.
- [ ] **[FEAT-6] 미사용 레거시 함수 정리:** TelemetryManager::notifySleepEnd() 등 야간 절전 기능 삭제 후 남은 데드 코드 클린업.

---

## 관련 문서 및 소스코드

* **메인 소스코드:** [main.cpp](../src/main.cpp)
* **사용자 매뉴얼:** [USER_MANUAL.md](USER_MANUAL.md)
* **배선 가이드:** [WIRING_GUIDE.md](WIRING_GUIDE.md)
* **하드웨어 부품 목록:** [HARDWARE_BOM.md](../HARDWARE_BOM.md)
* **MQTT 아키텍처 명세:** [mqtt_implementation_plan.md](mqtt_implementation_plan.md)
