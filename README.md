# 🗑️ SmartBox: ESP32-C6 기반 스마트 수거함 컨트롤러

초음파 센서로 사용자의 접근을 감지하고, 릴레이와 선형 액추에이터(Linear Actuator)를 제어하여 무거운 수거함 뚜껑을 자동으로 안전하게 개폐하는 스마트 제어 시스템입니다.

---

## ✨ 주요 기능 (Features)
* **비접촉식 개폐:** 방수 초음파 센서를 활용하여 50cm 이내 접근 시 자동 개방.
* **안전 최우선 설계 (Hardware Interlock):** 모터의 방향 전환 시 발생할 수 있는 쇼트(단락)를 방지하기 위해, 1채널 릴레이로 메인 12V 전원을 먼저 차단한 후 2채널 릴레이의 방향을 전환하는 2단 안전 구조 채택.
* **배터리 효율화:** 개폐 동작이 끝나면 대기 시간 동안 액추에이터로 향하는 모든 대기 전력을 완벽히 차단.
* **무선 펌웨어 업데이트 (Web Upload / HTTPS NAS Pull):** 웹 대시보드에서 직접 `.bin` 파일을 업로드하여 업데이트하는 방식과 사용자 소유의 HTTPS NAS 서버로부터 직접 최신 펌웨어를 다운로드하여 업데이트하는 Pull 방식을 모두 지원합니다. 업데이트 시작 즉시 모든 릴레이 전원을 물리적으로 완전 격리(고임피던스 INPUT 및 FSM 동결)하는 **Pre-OTA Hardware Interlock**이 동일하게 작용합니다.

---

## 🛠️ 하드웨어 명세서 (BOM & Specifications)

상세한 하드웨어 부품 목록 및 사양은 [HARDWARE_BOM.md](HARDWARE_BOM.md)를 참고하세요.

안정적인 구동을 위해 제어부(5V)와 구동부(12V)의 전원을 분리하여 설계했습니다.

### 1. 제어부 (Controller & Sensor)
| 부품명 | 규격 및 스펙 | 역할 |
| :--- | :--- | :--- |
| **MCU** | `ESP32-C6-DevKitC-1` (16MB Flash, 160MHz) | 시스템 메인 두뇌 (Wi-Fi 6, BLE 5 지원) |
| **초음파 센서** | `AJ-SR04T` | 야외/오염 환경에 강한 방수형 거리 측정 센서 |

### 2. 구동부 (Actuator & Relays)
| 부품명 | 규격 및 스펙 | 역할 |
| :--- | :--- | :--- |
| **선형 액추에이터** | 12V / 200mm 스트로크 / 60mm/s 속도 | 뚜껑을 물리적으로 밀어 올리고 닫는 모터 (내장 리밋 스위치 포함) |
| **1채널 릴레이** | 5V 제어 / 접점 용량 10A 이상 | 액추에이터로 가는 메인 12V 전원 컷오프 (안전 스위치) |
| **2채널 릴레이** | 5V 제어 / 접점 용량 10A 이상 | 액추에이터의 극성(+, -)을 반전시켜 정회전/역회전 제어 |

### 3. 전원 공급 (Power Supply)
* **제어부 전원:** 5V USB Type-C (또는 5V 강압 모듈 사용)
* **모터 전원:** 12V 납축전지 (또는 12V 고출력 어댑터)

---

## 🔌 핀 맵핑 및 결선 가이드 (Wiring Diagram)

🚨 **경고:** 제어용 5V와 모터용 12V 선이 절대 섞이지 않도록 주의하십시오. 12V가 ESP32 핀에 닿는 순간 보드가 파손됩니다.

### 센서 및 제어 신호 핀맵
| ESP32-C6 핀 | 부품 핀 | 설명 |
| :---: | :---: | :--- |
| `5V (VBUS)` | 센서/릴레이 `VCC` | 5V 전원 공통 공급 |
| `GND` | 센서/릴레이 `GND` | 접지 공통 연결 |
| `GPIO 4` | AJ-SR04T `Trig` | 초음파 발신 트리거 |
| `GPIO 5` | AJ-SR04T `Echo` | 초음파 수신 에코 |
| `GPIO 6` | 1-Ch Relay `IN` | **[STEP 1]** 메인 전원(12V) 통과/차단 제어 |
| `GPIO 7` | 2-Ch Relay `IN1` | **[STEP 2]** 모터 정회전 (뚜껑 열기) |
| `GPIO 8` | 2-Ch Relay `IN2` | **[STEP 2]** 모터 역회전 (뚜껑 닫기) |

---

## ⚙️ 소프트웨어 환경 및 펌웨어 빌드

본 프로젝트는 최신 칩셋인 ESP32-C6를 아두이노 프레임워크로 구동하기 위해 커스텀 저장소(`pioarduino`)를 사용합니다.

### 요구 사항
* **IDE:** VS Code 또는 Antigravity IDE
* **Extension:** PlatformIO IDE
* **OS:** Windows / macOS / Linux

### 빌드 및 업로드 방법
1. 레포지토리를 클론하고 PlatformIO 환경에서 프로젝트 폴더를 엽니다.
2. **보안 설정 (Secrets Setup):**
   - `include/secrets.h.example` 파일을 복사하여 `include/secrets.h` 파일을 생성합니다.
   - `include/secrets.h` 파일 내에 실제 Wi-Fi 정보, InfluxDB 토큰 및 주소, Synology NAS 주소를 기입합니다.
   - `include/secrets.h` 파일은 민감 정보 노출 방지를 위해 `.gitignore`에 등록되어 외부 저장소에 올라가지 않습니다.
3. 보드를 PC와 USB 케이블로 연결합니다. 
   *(주의: 보드에 2개의 USB 포트가 있다면 반드시 **`CH343 (UART)`** 포트에 연결하세요.)*
4. 장치 관리자에서 할당된 COM 포트 번호(예: `COM4`)를 확인합니다.
5. `platformio.ini` 파일 하단의 `upload_port` 값을 본인의 환경에 맞게 수정합니다.
6. PlatformIO 하단의 **`➔ (Upload)`** 버튼을 눌러 컴파일 및 펌웨어 플래싱을 진행합니다.

### 🌐 CI/CD 파이프라인 보안 연동 (GitHub Actions)
GitHub Actions를 통한 자동 배포 시 소스 코드와 마찬가지로 `secrets.h`가 누락되어 빌드가 실패하는 것을 방지하기 위해, 리포지토리의 **GitHub Secrets** 설정을 읽어 빌드 전 `secrets.h` 파일을 동적 생성합니다. GitHub 리포지토리의 `Settings > Secrets and variables > Actions` 메뉴에서 아래 항목들을 등록해야 합니다:
- `WIFI_SSID`: 기본 WiFi SSID 명
- `WIFI_PASS`: 기본 WiFi 비밀번호
- `INFLUXDB_URL`: InfluxDB 서버 주소
- `INFLUXDB_TOKEN`: InfluxDB 인증용 API 토큰
- `INFLUXDB_ORG`: InfluxDB 조직 명
- `INFLUXDB_BUCKET`: InfluxDB 텔레메트리 버킷 명
- `VERSION_URL`: 펌웨어 버전 manifest.json 원격 URL
- `FIRMWARE_URL`: 펌웨어 바이너리(.bin) 원격 URL

---

## ⚠️ 하드웨어 제어 시 주의사항 (CRITICAL)

본 프로젝트의 핵심은 **모터 쇼트 방지**입니다. 2채널 릴레이를 이용해 모터의 방향을 바꿀 때, 코딩 실수나 딜레이 부족으로 인해 IN1과 IN2가 동시에 켜지면 12V 배터리가 단락(Short)되어 화재나 릴레이 융착이 발생할 수 있습니다.

* **안전 규칙:** 코드를 수정할 때는 반드시 1채널 릴레이(`GPIO 6`)를 `LOW`로 낮춰 물리적으로 12V 전원을 끊은 상태에서만 2채널 릴레이(`GPIO 7`, `8`)의 상태를 변경해야 합니다.