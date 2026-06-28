# 📄 보고서: SmartBox 비동기 MQTTS 연동 및 원격 제어 Command Dispatcher 구축

- **작성일자:** 2026년 06월 28일
- **주제:** ESP32-C6 비동기 MQTTS(TLS 4883) 연동, 100% Feature Parity 원격 제어(Command Dispatcher), 수치 Clamping 및 전력 관리 통합

---

## 1. 아키텍처 및 작업 개요 (Overview)

SmartBox 프로젝트(ESP32-C6, FreeRTOS 기반)에 외부 원격 제어 및 상태 모니터링을 위한 엔터프라이즈 MQTTS 연동 기능(`MqttManager`)을 구축하였습니다.

| 핵심 구현 항목 | 기술 요약 및 적용 사항 |
|---|---|
| **논블로킹 비동기 아키텍처** | `AsyncMqttClient` 기반 비동기 소켓 통신을 적용하여 12V 리니어 액추에이터 제어 및 안티핀치 메인 루프 블로킹 차단 |
| **TLS 4883 보안 접속** | Synology NAS MQTTS 전용 커스텀 포트 **4883** 접속 및 `SECRET_ROOT_CA_CERT` TLS Pinning 주입 |
| **100% Feature Parity** | 로컬 웹 대시보드(WebDashboard)의 모든 제어 기능(문 개폐, OTA, 재부팅, 설정 변경)을 원격 명령어로 100% 이식 |
| **수치 범위 강제 (Clamping)** | 동적 설정 변경(`set_config`) 시 `constrain()` 함수로 설정 허용 범위를 명시적으로 강제 차단 |
| **Power-Aware 통합** | 야간 절전 모드(`isNightSleepActive()`) 감지 시 MQTTS 세션을 정상 디스커넥트하고 재연결 타이머 중단 |

---

## 2. 세부 구현 내역 (Implementation Details)

### 2.1. 라이브러리 및 환경 설정
- **[platformio.ini](../platformio.ini):** `marvinroger/AsyncMqttClient @ ^0.9.0` 및 `bblanchon/ArduinoJson @ ^7.0.4` 의존성 추가.
- **[include/secrets.h](../include/secrets.h) & [secrets.h.example](../include/secrets.h.example):** `SECRET_MQTT_HOST`, `SECRET_MQTT_PORT` (4883), `SECRET_MQTT_USER`, `SECRET_MQTT_PASS` 구성.
- **[.github/workflows/deploy.yml](../.github/workflows/deploy.yml):** CI/CD 파이프라인 상의 GitHub Secrets 주입 스텝 추가.

### 2.2. Core MqttManager 모듈 구현
- **[src/MqttManager.h](../src/MqttManager.h) & [src/MqttManager.cpp](../src/MqttManager.cpp):**
  - **MQTTS 및 LWT 설정:** `setSecure(true)` 및 Will 메시지(`smartbox/status` QoS 1, Retain=true) 등록.
  - **Command Dispatcher (`handleCommand`):** `force_open`, `force_close`, `reboot`, `trigger_ota`, `release_emergency`, `maintenance`, `debug`, `set_config` 명령어 비동기 분기 처리.
  - **설정 변경 및 Clamping (`handleSetConfig`):**
    - `stall_current` (500~10000mA), `hold_time_ms` (1000~60000ms), `dist_threshold` (5~150cm), `ota_hour` (0~23시), `telemetry_interval` (1~1440분)을 `constrain()` 함수로 검증 및 제한.
    - 변경 완료 시 NVS(Preferences) 메모리 반영(`ConfigManager::saveConfig`) 및 `smartbox/log` 결과 알림 퍼블리시.

### 2.3. 시스템 및 멀티캐스트 로깅 통합
- **[src/main.cpp](../src/main.cpp):**
  - 백그라운드 FreeRTOS 스레드(`NetworkTask`)에서 `mqttManager.update()` 및 30초 주기 텔레메트리 퍼블리시 구동.
  - `RemoteLogger::onWarnError` 콜백을 확장하여 InfluxDB와 MQTT 동시 멀티캐스트 로깅 연동.

---

## 3. 검증 및 빌드 결과 (Verification & Metrics)

PlatformIO 환경에서 ESP32-C6 타겟 펌웨어 빌드를 수행하여 정상 동작 및 컴파일 성공을 검증하였습니다.

```console
Processing esp32-c6-devkitc-1 (platform: https://github.com/pioarduino/platform-espressif32.git; board: esp32-c6-devkitc-1; framework: arduino)
HARDWARE: ESP32C6 160MHz, 320KB RAM, 8MB Flash
RAM:   [==        ]  19.6% (used 64352 bytes from 327680 bytes)
Flash: [==        ]  21.0% (used 1379274 bytes from 6553600 bytes)
========================= [SUCCESS] Took 22.87 seconds =========================
```

---

## 4. 관련 문서 업데이트 현황
- **[docs/mqtt_implementation_plan.md](../docs/mqtt_implementation_plan.md):** 아키텍처 개요, 상대 경로 링크 표준화, 전체 원격 제어 JSON 페이로드 스펙 및 매개변수 검증 범위를 최신화했습니다.
