# 📋 개발 및 검증 보고서: MQTT 배치 & 하이브리드 이벤트 텔레메트리 구현 및 통신 단일화

**보고서 번호:** 26063030  
**작성일자:** 2026-06-30  
**작성자:** AI Coding Assistant (Antigravity) & Lead Engineer  
**타겟 시스템:** SmartBox (ESP32-C6 메인 제어기)  

---

## 1. 개요 및 배경

SmartBox의 전력 효율 극대화 및 하드웨어 수명 모니터링 강화를 위하여, 기존 정기 텔레메트리 방식에서 **"타임스탬프 기반 RAM 링 버퍼 배치(Batching) 송신"** 및 **"상태변화/모션/알람 실시간 하이브리드 이벤트(Hybrid Event)"** 구조로 MQTT 텔레메트리 통신 체계를 전환하고 통합 구현하였습니다.

또한, 디바이스의 네트워크 및 메모리 자원 중복 소모를 방지하기 위해 **InfluxDB 직접 HTTP 송신 모듈을 완전 제거하고 MQTT over TLS 단일 파이프라인으로 시스템을 슬림화**하였습니다.

### 주요 개선 목표
1. **전력 소모 절감**: 센서 측정 시마다 발생하던 패킷 송신을 RAM 링 버퍼 배치 송신으로 변경하여 Wi-Fi RF 가동 시간 최소화.
2. **실시간 모니터링**: 상태 변경, 100cm 이내 모션 진입, 모터 과전류(Stall) 및 저전압 알람 시 즉시(Instant) 토픽 전송.
3. **액추에이터 건강 상태 추적**: 1회 개폐 동작 완료 시 동작 시간, 피크 전류, 배터리 Drop 폭을 담은 사이클 리포트 발행.
4. **통신 파이프라인 단일화**: 무거운 InfluxDB 라이브러리 및 듀얼 TLS 연결 제거로 플래시/RAM 메모리 및 전력 대폭 아낌.
5. **Home Assistant Auto Discovery**: HA **[기기 및 서비스]** 메뉴에 클릭 한 번 없이 `[SmartBox]` 기기로 자동 등록 기능 구현.

---

## 2. 세부 변경 사항 (Implementation Summary)

### 2.1. 신규 토픽 구조 및 기능

| 구분 | MQTT 토픽 | 발생 조건 / 주기 | 핵심 수집 필드 |
| :--- | :--- | :--- | :--- |
| **시계열 배치** | `smartbox/telemetry/batch` | N분 간격 주기적 일괄 퍼블리시 (QoS 0) | `ts`, `batt_v`, `dist_cm`, `curr_ma`, `state` 배열 |
| **모션 감지** | `smartbox/event/motion` | 거리 <= 100cm 구역 진입 시 엣지 트리거 1회 | `timestamp`, `event`, `distance_cm` |
| **상태 변경** | `smartbox/event/state` | 메인 FSM 상태 전이 시 즉시 발행 (QoS 1) | `timestamp`, `prev_state`, `new_state`, `battery_v` |
| **긴급 알람** | `smartbox/event/alarm` | 모터 과전류/저전압 셧다운 발생 즉시 (QoS 1) | `timestamp`, `alarm`, `value`, `message`, `battery_v` |
| **사이클 리포트**| `smartbox/event/cycle` | IDLE 복귀 시 1회 동작 통계 요약 | `timestamp`, `duration_ms`, `peak_current_ma`, `start_batt_v`, `end_batt_v` |
| **자동 등록** | `homeassistant/+/smartbox_01/+/config` | 부팅 시 Home Assistant MQTT Auto Discovery 발행 | HA 기기/센서 자동 생성을 위한 보일러플레이트 |

---

### 2.2. 소스 코드 변경 내역

* **[platformio.ini](../platformio.ini)**
  - 불필요한 `tobiasschuerg/ESP8266 Influxdb` 라이브러리 의존성 제거.
* **[src/MqttManager.h](../src/MqttManager.h) & [src/MqttManager.cpp](../src/MqttManager.cpp)**
  - 배치/이벤트 전용 메서드 구현 및 Home Assistant MQTT Auto Discovery (`publishAutoDiscovery`) 메서드 추가.
* **[src/TelemetryManager.h](../src/TelemetryManager.h) & [src/TelemetryManager.cpp](../src/TelemetryManager.cpp)**
  - InfluxDB HTTP 클라이언트 및 태스크 완전 제거. 순수 MQTT 배치 샘플링 및 송신 모듈로 경량화.
* **[src/main.cpp](../src/main.cpp)**
  - `SmartBoxController`의 `stateCallback` 등록 및 100cm 모션 엣지 트리거 연동.

---

## 3. 검증 결과 (Verification Results)

1. **소프트웨어 빌드 검증 (PlatformIO):**  
   - ESP32-C6 메인 프로덕션 타겟(`esp32-c6-devkitc-1`) 컴파일 검증 성공 (`SUCCESS`).
2. **메모리 및 자원 점검:**  
   - InfluxDB 라이브러리 제거로 바이너리 용량 감소 및 RAM 힙(Free Heap) 여유 공간 대폭 증가.
3. **문서화 반영:**  
   - 아키텍처 설계 문서 ([docs/mqtt_implementation_plan.md](../docs/mqtt_implementation_plan.md)) 사양 업데이트 완료.

---

**[보고서 끝]**
