# SmartBox 통합 사용자 매뉴얼 (Update 2026-07-20)

> **문서 버전:** 최신 커밋 기준 (2026-07-20)  
> **펌웨어 기준 브랜치:** main (dbcfff6)

본 매뉴얼은 태양광 충전 시스템과 초음파 센서를 결합한 스마트 수거함의 안전한 운영, Home Assistant 스마트홈 앱 연동 및 비상 대응을 위한 가이드입니다.

---

## 1. 시스템 구성 부품

* **메인 제어부:** ESP32-C6 (비동기 논블로킹 FSM, MQTTS over TLS, Home Assistant 자동 등록)
* **센서부:** 방수 초음파 센서 (AJ-SR04T), INA219 직류 전압/전류 모니터링 모듈 (I2C, GPIO 22/23)
* **구동부:** 12V 선형 액추에이터 2개 (병렬), 1채널 릴레이 (메인 12V 전원 차단 / GPIO 6), 2채널 릴레이 (정/역회전 방향 제어 / GPIO 7, GPIO 8)
* **전원부:** 태양광 패널, Solar Charge Controller, 12V 납축전지

---

## 2. 핵심 하드웨어 안전 수칙

이 시스템은 12V 고전류 모터와 기계식 릴레이를 제어하므로 아래 수칙을 반드시 엄수하십시오.

1. **릴레이 인터락 (Relay Interlock):** 정회전 릴레이(GPIO 7)와 역회전 릴레이(GPIO 8)를 동시에 ON(Active-Low) 상태로 출력해서는 안 됩니다. 이는 12V 배터리 단락(쇼트)과 화재를 유발합니다.
2. **전원 선-차단 후-방향 전환:** 모터 방향 전환 시, 1채널 메인 릴레이(GPIO 6)를 먼저 고임피던스(INPUT) 상태로 전환하여 전원을 차단하고, **100ms 가드 타임** 대기 후 방향 릴레이를 스위칭합니다.
3. **고임피던스 대기 모드:** IDLE 상태에서는 모든 릴레이 제어 핀을 INPUT(고임피던스) 상태로 전환하여 대기 전력을 0mA로 유지합니다.

---

## 3. Home Assistant 스마트홈 제어 및 자동 등록 (MQTT Integration)

### 3.1. 최초 설정 및 Wi-Fi 변경 (부팅 후 5분 온디맨드 프로비저닝)

1. 스마트박스 전원을 켭니다. **전원 ON 후 5분간 프로비저닝 AP가 활성화**됩니다.
2. 모바일/PC Wi-Fi 목록에서 **SmartBox-WiFi** 를 선택하여 연결합니다. (기본 비밀번호: smartbox_admin)
3. 브라우저에서 **http://192.168.4.1/** 를 열면 Wi-Fi 설정 페이지가 나타납니다.
   - **Scan Networks** 버튼으로 주변 AP를 자동 검색할 수 있습니다.
4. 연결할 공유기의 SSID와 비밀번호를 입력하고 **Save & Connect** 를 누릅니다.
5. 웹 화면에서 실시간으로 접속 상태가 확인됩니다:
   * **Connecting to...**: 공유기 접속 시도 중
   * **Connected! IP: 192.168.x.x**: 접속 성공 (MQTT 관제 개시)
   * **Connection failed**: 접속 실패 (비밀번호 오류 또는 SSID 신호 약함)

> 전원 ON 후 5분이 지나면 AP 전파 송출이 자동으로 중단되고, 기기는 Station 모드로만 동작합니다. Wi-Fi 재설정이 필요하면 **재부팅** 후 5분 이내에 진행하십시오.

---

### 3.2. Home Assistant 제어 방법

1. 스마트박스 전원을 켜고 Wi-Fi 및 MQTT 브로커에 접속하게 합니다.
2. Home Assistant **[설정] > [기기 및 서비스] > [MQTT]** 로 이동합니다.
3. **SmartBox** 기기와 아래 엔티티들이 자동으로 등록됩니다.
4. 별도의 configuration.yaml 코딩 없이 즉시 대시보드 카드로 원격 제어할 수 있습니다.

---

### 3.3. 지원 엔티티 목록

#### 원격 제어 버튼 (Buttons)

| 버튼 이름 | MQTT 명령 | 설명 |
| :--- | :--- | :--- |
| [SmartBox] 수거함 강제 열기 | force_open | 원격으로 즉시 뚜껑 개방 |
| [SmartBox] 수거함 강제 닫기 | force_close | 원격으로 뚜껑 닫기 |
| [SmartBox] 비상 정지 락 해제 | reset_emergency | 과전류 보호 비상 상태 수동 리셋 |
| [SmartBox] 펌웨어 강제 업데이트 (OTA) | trigger_ota | NAS로부터 OTA 업데이트 즉시 실행 |
| [SmartBox] 재부팅 | reboot | 원격 ESP32-C6 재시작 |
| [SmartBox] 디버그 로깅 5분 활성화 | debug on | 상세 디버그 로그 5분간 MQTT 전송 |
| [SmartBox] 정비 모드 시작 (5분) | maintenance start | 5분 정비 모드 진입 (뚜껑 열린 상태 유지) |

#### 실시간 텔레메트리 센서 (Sensors)

| 센서 이름 | 토픽 | 설명 |
| :--- | :--- | :--- |
| [SmartBox] 작동 상태 | smartbox/event/state | 실시간 FSM 상태 (IDLE, OPENING, HOLD 등) |
| [SmartBox] 배터리 전압 | smartbox/event/state | 실시간 배터리 전압 (V) |
| [SmartBox] 최근 개폐 소요 시간 | smartbox/event/cycle | 1회 개폐 동작 시간 (초) |
| [SmartBox] 최근 동작 피크 전류 | smartbox/event/cycle | 액추에이터 피크 전류 (mA) |
| [SmartBox] 센서 거리 | smartbox/telemetry | 초음파 센서 측정 거리 (cm) |
| [SmartBox] 와이파이 신호 강도 | smartbox/telemetry | Wi-Fi RSSI (dBm) |
| [SmartBox] 연결 상태 | smartbox/status | 온라인/오프라인 연결 상태 |
| [SmartBox] 모션 감지 | smartbox/event/motion | 100cm 이내 접근 시 ON |

#### 원격 설정 변경 (Select Presets)

Home Assistant UI에서 드롭다운 선택만으로 설정값을 변경할 수 있습니다:

| 설정 이름 | 키 | 기본값 | 범위 |
| :--- | :--- | :--- | :--- |
| [SmartBox] 추천 설정: 감지 거리 (cm) | dist | 50.0 cm | 5~150 cm |
| [SmartBox] 추천 설정: 열림 유지 (ms) | wait | 10000 ms | 1,000~60,000 ms |
| [SmartBox] 추천 설정: 모터 제한 (mA) | stall | 3000 mA | 500~10,000 mA |
| [SmartBox] 추천 설정: 자동 OTA 시각 (시) | otaHour | 0시 | 0~23시 |
| [SmartBox] 추천 설정: 데이터 전송 주기 (분) | telInterval | 10분 | 1~1,440분 |

---

## 4. 자동 안전 보호 시스템 (FSM States)

시스템은 11개 상태를 가진 유한 상태 머신(FSM)으로 안전 가드를 자율 제어합니다.

| 상태 | 설명 |
| :--- | :--- |
| IDLE | 대기 중. 모든 릴레이 고임피던스(0mA). 센서 감지 준비 |
| OPEN_START | 개방 시작. 방향 릴레이 설정 -> 인터락 적용 |
| OPENING | 개방 중. 액추에이터 구동 (3.8초 타임아웃) |
| HOLD | 뚜껑 열림 대기. 인체 감지 시 타이머 리셋 (기본 10초, 최대 3분) |
| CLOSE_START | 닫힘 시작. 방향 릴레이 반전 -> 인터락 적용 |
| CLOSING | 닫힘 중. 인체 재감지 시 즉시 재개방 |
| EMERGENCY_STOP | 과전류 비상 정지. 5초 후 자동 복구 또는 수동 리셋 |
| BATTERY_LOW_SHUTDOWN | 저전압(11.3V 이하, 3초 연속) 강제 개방 후 전원 완전 격리 |
| OTA_UPDATING | OTA 업데이트 중. 모든 릴레이 차단. FSM 동결 |
| MAINTENANCE | 정비 모드. 뚜껑 열린 상태 5분간 유지 후 자동 닫힘 |
| STARTUP_OPEN | 재시작 후 이전 열림 상태 감지 -> 홈(Homing) 닫힘 실행 |

### 주요 보호 동작

* **협착 방지 비상 정지 (STATE_EMERGENCY_STOP):**
  개폐 중 모터 전류가 currentStallLimit(기본 3,000mA)을 500ms 인러시 구간 이후 3회 연속 초과하면 즉시 비상 정지 및 릴레이 고임피던스 격리. 5초 후 자동 복구 또는 Home Assistant에서 수동 해제 가능.

* **저전압 배터리 보호 (STATE_BATTERY_LOW_SHUTDOWN):**
  배터리 전압이 11.3V 이하로 3초 이상 유지되면 뚜껑을 강제 완전 개방 후 릴레이를 영구 격리. OTA 진행 중에는 이 보호 로직이 일시 우회됩니다.

* **센서 데드락 방지 (Self-Healing):**
  HOLD 상태에서 최대 보유 시간(기본 3분) 초과 시 강제 닫힘을 실행. 이후 경로가 10초간 연속으로 청결(인체 미감지) 상태가 유지되면 자동으로 플래그가 해제됩니다.

* **새벽 결로 노이즈 방어:**
  센서 감지 후 300ms 디바운스 + 2cm 이하 이상값 무시 + 닫힘 직후 3초 블라인드 타임(기구물 안정화)이 적용됩니다.

* **자동 일일 재부팅 (PowerManager, 매일 04:00 AM):**
  NTP 동기화 후 새벽 4시, FSM이 IDLE 상태이고 모터가 정지해 있을 때 하루 1회 안전하게 자동 재부팅됩니다. Preferences NVS에 마지막 재부팅 날짜(reboot_day)를 기록하여 중복 실행을 방지합니다.

---

## 5. 문제 해결 가이드 (Troubleshooting)

| 현상 | 원인 분석 및 해결 조치 |
| :--- | :--- |
| **뚜껑이 열리지 않고 앱 반응이 없음** | 1. 12V 메인 전원 스위치와 배터리 잔량을 점검하십시오. 2. Home Assistant에서 배터리 전압이 11.3V 이하(BATTERY_LOW_SHUTDOWN)인지 확인하십시오. |
| **앱에서 기기가 오프라인으로 표시됨** | Wi-Fi RSSI([SmartBox] 와이파이 신호 강도) 및 MQTT 브로커 접속 상태를 확인하십시오. 재부팅 후 수초 내 자동 복구됩니다. |
| **새벽 4시경 연결이 잠시 끊겼다가 복구됨** | 정상 동작입니다. 시스템이 매일 04:00 AM에 자동 일일 재부팅을 수행하며, 재부팅 완료 후 15~30초 이내에 MQTT가 자동 재연결됩니다. |
| **뚜껑이 계속 열렸다 닫혔다를 반복함** | 센서 앞에 반사물이 있거나 결로가 발생했을 수 있습니다. 센서 주변을 청소하십시오. 데드락 방어 로직이 10초 후 자동으로 문제를 해제합니다. |
| **비상 정지(Emergency Stop)가 자주 발생함** | 과전류 임계값이 너무 낮게 설정되어 있습니다. Home Assistant에서 [SmartBox] 추천 설정: 모터 제한 (mA) 값을 높여보십시오. |
| **OTA 업데이트가 실패함** | Wi-Fi 신호 강도와 NAS 서버 접속 상태를 확인하십시오. Home Assistant에서 펌웨어 강제 업데이트 (OTA) 버튼으로 재시도할 수 있습니다. |

---

## 6. 관련 문서

* **개발 매뉴얼 (하드웨어/소프트웨어 상세):** [DEVELOPMENT_MANUAL.md](DEVELOPMENT_MANUAL.md)
* **배선 가이드:** [WIRING_GUIDE.md](WIRING_GUIDE.md)
* **하드웨어 부품 목록:** [HARDWARE_BOM.md](../HARDWARE_BOM.md)
* **메인 소스코드:** [main.cpp](../src/main.cpp)
