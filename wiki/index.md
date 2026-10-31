---
title: SmartBox wiki index
type: index
project: smartbox
status: active
updated: 2026-11-01
source_of_truth: true
---

# SmartBox Wiki

> 현장0.2.0-g7a55dd8은 측정 무효 상태이며0.2.1은 회귀 수정 후보다. 코드 구현·시험·실제 적용 상태는 [현재 상태](project_status.md)에서 구분한다.

## 먼저 읽기

| 문서 | 용도 |
|---|---|
| [project_status.md](project_status.md) | 최신 코드 구현, 확인된 증거, 남은 Gate |
| [architecture.md](architecture.md) | FSM, relay, sensor, Wi-Fi, MQTT, OTA 흐름 |
| [knowledge_management.md](knowledge_management.md) | Obsidian·LLM 문서 분류와 Hub 승격 규칙 |
| [log.md](log.md) | append-only 문서 변경 기록 |

## 현재형 매뉴얼

| 질문 | 문서 |
|---|---|
| 사용자 운영과 Home Assistant | [사용자 매뉴얼](../docs/USER_MANUAL.md) |
| 조립·개발·안전 계약 | [개발 매뉴얼](../docs/DEVELOPMENT_MANUAL.md) |
| 배선 | [배선 가이드](../docs/WIRING_GUIDE.md) |
| 부품 | [하드웨어 BOM](../HARDWARE_BOM.md) |
| 빌드와 시작 | [README](../README.md) |
| 민감 정보 이력 정리 | [보안 정리 가이드](../SECURITY_CLEANUP_GUIDE.md) |

## 역사 자료

- `reports/`: 구현·장애·수정 당시의 보고서. 파일명 순서로 이력을 추적한다.
- `prompts/`: 대화/작업 transcript. 현재 사실의 근거로 단독 사용하지 않는다.
- [implementation_plan.md](../docs/implementation_plan.md), [task.md](../docs/task.md), [walkthrough.md](../docs/walkthrough.md): 0.1.0 텔레메트리 작업 snapshot.
- [mqtt_implementation_plan.md](../docs/mqtt_implementation_plan.md): 초기 MQTT 제안. 현재 구현은 PsychicMqttClient 기반이므로 [architecture.md](architecture.md)를 우선한다.

## 불변조건

1. Active-Low relay에서 open/close 방향을 동시에 활성화하지 않는다.
2. 방향 변경 전 Main을 high-impedance로 끄고 접점 분리 시간을 둔다.
3. OTA 중 relay를 격리하고 actuator FSM을 멈춘다.
4. 비밀은 `include/secrets.h`에만 두며 저장소에 포함하지 않는다.
5. build, native test, 실기기, 배포 증거를 같은 것으로 표현하지 않는다.

## 열린 질문

- 최신 main firmware의 실제 ESP32-C6 설치·부팅·OTA 후 버전 확인 증거는 별도 기록이 필요하다.
- GPIO 4/5/8 부팅 특성과 현재 relay/sensor 회로의 전원 투입 동작은 실물에서 재검증해야 한다.
- AJ-SR04T ECHO의 ESP32-C6 3.3 V 입력 보호 방식이 배선 문서에 명시되어 있으나 현장 장착 증거는 별도 필요하다.
- OTA는 TLS와 16 MB 파티션을 사용하지만 signed artifact, dual-slot health confirmation, rollback 증거는 현재 코드에서 확인되지 않는다.

## Broker migration follow-up

- [mqtt_broker_migration.md](mqtt_broker_migration.md): combined named-principal migration, private/authenticated OTA protection, bounded legacy bootstrap and remaining device acceptance.

- Shared authenticated broker accepted2026-10-17; see [migration](mqtt_broker_migration.md) and [current status](project_status.md). Earlier baseline is historical.

- [ota_diagnostic_acceptance.md](ota_diagnostic_acceptance.md): bounded manifest failure, authentication/expiry fixture and actual installed-device recovery gate.

- [battery_power_management.md](battery_power_management.md): automatic low-voltage recovery, measurement validity, bounded savings and NAS/HA monitoring with installed-evidence boundaries.

- [ina219_regression.md](ina219_regression.md): field-before/after evidence, continuous-reader rollback candidate, numeric failure diagnostics and the confirmed0.2.0 recovery-OTA lockout.

- [field_recovery.md](field_recovery.md): owner-private offline USB firmware kit, equipment, guarded app0/otadata recovery and physical acceptance procedure.
