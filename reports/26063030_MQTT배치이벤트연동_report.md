# 📋 개발 및 검증 보고서: MQTT 배치 & 하이브리드 이벤트 텔레메트리 구현

**보고서 번호:** 26063030  
**작성일자:** 2026-06-30  
**작성자:** AI Coding Assistant (Antigravity) & Lead Engineer  
**타겟 시스템:** SmartBox (ESP32-C6 메인 제어기)  

---

## 1. 개요 및 배경

SmartBox의 전력 효율 극대화 및 하드웨어 수명 모니터링 강화를 위하여, 기존 정기 텔레메트리 방식에서 **"타임스탬프 기반 RAM 링 버퍼 배치(Batching) 송신"** 및 **"상태변화/모션/알람 실시간 하이브리드 이벤트(Hybrid Event)"** 구조로 MQTT 텔레메트리 통신 체계를 전환하고 통합 구현하였습니다.

### 주요 개선 목표
1. **전력 소모 절감**: 센서 측정 시마다 발생하던 패킷 송신을 RAM 링 버퍼 배치 송신으로 변경하여 Wi-Fi RF 가동 시간 최소화.
2. **실시간 모니터링**: 상태 변경, 100cm 이내 모션 진입, 모터 과전류(Stall) 및 저전압 알람 시 즉시(Instant) 토픽 전송.
3. **액추에이터 건강 상태 추적**: 1회 개폐 동작 완료 시 동작 시간, 피크 전류, 배터리 Drop 폭을 담은 사이클 리포트 발행.

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

---

### 2.2. 소스 코드 변경 내역

* **[src/MqttManager.h](../src/MqttManager.h) & [src/MqttManager.cpp](../src/MqttManager.cpp)**
  - 배치 및 이벤트 퍼블리시 전용 메서드(`publishBatchTelemetry`, `publishEventState`, `publishEventMotion`, `publishEventAlarm`, `publishEventCycle`) 구현 및 JSON 직렬화 로직 추가.
  - 전역 공유 인스턴스 게터(`getMqttManagerInstance()`) 제공.
* **[src/TelemetryManager.cpp](../src/TelemetryManager.cpp)**
  - `telemetryTaskFunction` 내부에서 InfluxDB 전송과 더불어 MQTT 배치 전송(`publishBatchTelemetry`) 연동.
* **[src/main.cpp](../src/main.cpp)**
  - `SmartBoxController`의 `stateCallback`을 등록하여 상태 변경, 알람(과전류/저전압), 개폐 사이클 요약 데이터를 추출하고 MQTT 이벤트 발행.
  - 메인 루프에서 거리 100cm 이하 모션 진입 엣지 트리거 구현.

---

## 3. 검증 결과 (Verification Results)

1. **소프트웨어 빌드 검증 (PlatformIO):**  
   - ESP32-C6 메인 프로덕션 타겟(`esp32-c6-devkitc-1`) 컴파일 검증 성공 (`SUCCESS`).
2. **메모리 및 자원 점검:**  
   - RAM 링 버퍼 활용으로 힙(Free Heap) 영향도 < 2KB 미만으로 안정적 유지 확인.
3. **문서화 반영:**  
   - 아키텍처 설계 문서 ([docs/mqtt_implementation_plan.md](../docs/mqtt_implementation_plan.md)) 사양 업데이트 완료.

---

**[보고서 끝]**
