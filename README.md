# 🚀 SmartBox: High-Availability Autonomous SmartBox Controller

![Platform](https://img.shields.io/badge/Platform-PlatformIO-orange.svg)
![Framework](https://img.shields.io/badge/Framework-Arduino-blue.svg)
![Board](https://img.shields.io/badge/Board-ESP32--C6-red.svg)
![MQTT](https://img.shields.io/badge/Protocol-MQTTS%20TLS-green.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

**SmartBox**는 단순한 토이 프로젝트가 아닌, **엔터프라이즈 IoT 수준의 고가용성 및 자율 운영 설계**가 적용된 차세대 스마트 수거함 컨트롤러입니다. 
초음파 센서로 사용자의 접근을 이중 필터링하여 감지하고, 이중 릴레이와 선형 액추에이터(Linear Actuator)를 제어하여 무거운 수거함 뚜껑을 완벽하게 통제합니다. 최저 전력 소모, 실시간 텔레메트리 관제, 자율 무선 펌웨어 업데이트 등 어떠한 가혹한 실외 환경에서도 시스템이 다운되지 않도록 다중 안전장치가 설계되어 있습니다.

---

## ✨ 핵심 기능 (Enterprise IoT Features)

* **비동기 FSM 및 하드웨어 인터락 (Hardware Interlock)**
  * **이중 노이즈 필터링:** 5샘플 중간값(Median Filter) 기반의 센서 노이즈 처리로 오작동을 차단합니다.
  * **협착 방지(Anti-Pinch) 긴급 정지:** 구동 중 INA219 전력 센서를 통해 모터 실속(Stall) 전류를 감시하며, 3000mA 초과 시 3회 연속 감지되면 즉각 비상 정지(Emergency Stop) 상태로 전이합니다.
  * **이중 릴레이 안전망:** 방향 릴레이 전환 시 발생 가능한 쇼트를 막기 위해, 1채널 릴레이로 메인 12V 전원을 먼저 컷오프하는 하드웨어 및 소프트웨어 인터락을 강제합니다.

* **모던 MQTTS over TLS & Home Assistant 통합**
  * **단일 통신 파이프라인 (Single Pipeline):** 무거운 InfluxDB 및 레거시 웹 서버를 완전 철거하고 MQTTS over TLS (포트 4883) 단 하나로 통신을 단일화하여 전력과 메모리 자원을 극대화하여 아꼈습니다.
  * **Home Assistant MQTT Auto Discovery:** 펌웨어가 부팅 시 자기소개(Discovery) 패킷을 발행하여, 사용자가 Home Assistant 설정 파일(`configuration.yaml`)에 코드를 적을 필요 없이 클릭 한 번으로 `[SmartBox]` 기기 및 11개 엔티티가 자동 등록됩니다.

* **자율 운영 및 무인 OTA (Over-The-Air)**
  * **매일 심야 무인 업데이트:** 지정된 HTTPS NAS(Network Attached Storage) 서버에 배포된 `version.json` 매니페스트를 자동으로 Pull 방식으로 폴링하여, 펌웨어 버전의 차이가 있을 때만 백그라운드 OTA를 안전하게 수행합니다.
  * **Pre-OTA Hardware Isolation:** 업데이트 개시 즉시 모든 릴레이 전원을 물리적 고임피던스(INPUT)로 완전 격리하여 업데이트 도중의 노이즈로 인한 쇼트 화재를 원천 봉쇄합니다.

* **시계열 배치 텔레메트리 및 하이브리드 이벤트**
  * **RAM Ring Buffer 배치 전송:** 배터리 전압 및 상태 데이터를 1분 간격으로 RAM 버퍼에 모았다가 일괄 전송하여 Wi-Fi RF 가동 소모 전력을 90% 이상 아낍니다.
  * **실시간 알람 및 개폐 사이클 요약:** 과전류/저전압 즉시 알람 발행, 100cm 모션 감지 엣지 트리거 및 1회 개폐 동작 완료 시 피크 전류와 동작 시간을 담은 요약 리포트를 발행합니다.

* **극저전력 및 고가용성 확보 (High-Availability)**
  * **Proactive Reboot (매일 04:00 AM):** NTP 동기화된 시각 기준으로 매일 새벽 4시 FSM이 IDLE 상태일 때 안전하게 자동 재부팅하여 메모리 단편화 및 장기간 운영 중 발생할 수 있는 상태 누적 문제를 예방합니다.
  * **하드웨어 Watchdog (TWDT):** 10초 타임아웃의 듀얼 코어 Task Watchdog Timer(TWDT)가 상시 가동되어, 통신 지연이나 프리징 시 자가 복구합니다.
  * **저전압 셧다운 방어 (`STATE_BATTERY_LOW_SHUTDOWN`):** 배터리가 11.3V 이하로 3초 이상 지속될 경우 남은 전력으로 수거함을 완전 개방시킨 뒤 영구 슬립에 진입하여 관리자의 정비(배터리 교체)를 용이하게 합니다.

---

## 🛠️ 하드웨어 사양 (Hardware Specifications)

상세한 하드웨어 부품 목록 및 튜닝 값은 [HARDWARE_BOM.md](HARDWARE_BOM.md)를 참고하세요. 안정적인 구동을 위해 제어부(5V)와 구동부(12V)의 전원을 완전히 분리하여 설계했습니다.

* **제어부 (Controller):** `ESP32-C6-DevKitC-1` (16MB Flash, 160MHz) - 최신 Wi-Fi 6 지원
* **센서부 (Sensors):** `AJ-SR04T` 방수 초음파 거리 센서, `INA219` 직류 전압/전류 모니터링 모듈
* **구동부 (Actuators):** 12V 100N 리니어 액추에이터 x 2 (병렬), 1채널 릴레이(전원 차단용), 2채널 릴레이(정/역 방향 전환용)
* **전원부 (Power):** 12V 7Ah 밀폐형 연납축전지, 30W 태양광 패널 및 PWM 충전 컨트롤러, LM2596 강압 모듈

---

## 🔌 핀 맵핑 및 결선 가이드 (Wiring Diagram)

🚨 **경고:** 제어용 5V와 모터용 12V 선이 절대 혼재되지 않도록 극도로 주의하십시오. 12V가 ESP32 핀에 혼입되는 순간 보드가 즉시 소손됩니다.

| ESP32-C6 핀 | 부품 핀 | 설명 및 인터락 역할 |
| :---: | :---: | :--- |
| `5V (VBUS)` | 센서/릴레이 `VCC` | 5V 전원 공통 공급 |
| `GND` | 센서/릴레이 `GND` | 접지 공통 연결 |
| `GPIO 4` | AJ-SR04T `Trig` | 초음파 발신 트리거 |
| `GPIO 5` | AJ-SR04T `Echo` | 초음파 수신 에코 |
| `GPIO 6` | 1-Ch Relay `IN` | **[STEP 1 안전망]** 메인 전원(12V) 통과/차단 제어 |
| `GPIO 7` | 2-Ch Relay `IN1` | **[STEP 2 방향]** 모터 정회전 (뚜껑 열기) |
| `GPIO 8` | 2-Ch Relay `IN2` | **[STEP 2 방향]** 모터 역회전 (뚜껑 닫기) |
| `GPIO 22` | INA219 `SDA` | 전류 모니터 I2C 데이터 라인 |
| `GPIO 23` | INA219 `SCL` | 전류 모니터 I2C 클럭 라인 |

---

## ⚙️ 소프트웨어 환경 및 펌웨어 빌드

이 프로젝트는 최신 칩셋인 ESP32-C6를 지원하고, 비동기 네트워크 처리를 최적화하기 위해 커스텀 저장소(`pioarduino`) 아두이노 프레임워크를 기반으로 합니다.

### 빌드 및 업로드 방법
1. 저장소를 클론하고 VS Code 또는 Antigravity IDE의 PlatformIO 환경에서 프로젝트를 엽니다.
2. **보안 설정 (Secrets Setup):**
   - `include/secrets.h.example` 파일을 복사하여 `include/secrets.h`를 만듭니다.
   - 내부 설정 파일에 로컬 SoftAP 비밀번호, Wi-Fi 정보, MQTT 접속 정보, HTTPS NAS 대상 루트 CA 인증서를 기입합니다.
   - *이 파일은 `.gitignore`에 등록되어 외부 저장소로 절대 푸시되지 않습니다.*
3. ESP32-C6 보드를 PC와 연결합니다. (2개의 USB 포트가 있다면 **`CH343 (UART)`** 포트에 연결하십시오.)
4. PlatformIO에서 `esp32-c6-devkitc-1` 환경으로 컴파일 및 펌웨어 플래싱을 진행합니다. (포트번호는 자동 감지됩니다.)

### 🌐 CI/CD 파이프라인 연동
GitHub Actions를 통한 자동 배포 파이프라인이 포함되어 있습니다. `.github/workflows/deploy.yml`는 Push 발생 시 네이티브 로직 테스트를 수행하고, 성공 시 ESP32-C6 타겟 바이너리를 빌드하여 Synology NAS 서버로 SFTP 자동 배포합니다. 이 과정에서 필요한 멀티라인 Root CA 등의 보안 변수들은 GitHub Secrets를 통해 동적으로 주입됩니다.