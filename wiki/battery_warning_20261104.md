# 배터리 경고:11.0V 이하 연속5분 — 2026-11-04

## 요청과 범위

소유자가 배터리 경고도11V 이하5분 연속일 때 적용하도록 요청했다. **경고/알림**과 **구동 보호**를 분리한다. 새0.2.4 후보에서 소유자 저전압 경고는 유효 전압이11.0V 이하로300초 연속 측정됐을 때만 켜진다. 일반 보호 진입11.0V 이하3초, 구동 시작 거절과 충전용 닫기의11.0V 이하 즉시 중단, 센서 오류 즉시 보호는 유지한다. 보호 해제12.0V/300초도 유지한다.

이전에 사용한 WARNING/SAVING/PROTECTED는 내부 전원 모드다. WARNING의 내부 절전 분류는 기존대로 남지만 새 소유자 저전압 경고 신호와 같지 않다. NAS에서는 내부 code1을 POWER_CAUTION으로 표시하고 이 모드 변경만으로 배터리 경고를 발송하지 않는다. 실제 FSM 보호 상태를 정상으로 위장하지 않는다.

## 장치의 연속 측정 증명

[BatteryPolicy](../src/BatteryPolicy.cpp)가 별도 timer를 유지한다.11.000V를 포함하고11.001V 이상이면 초기화한다. 무효 측정이나2초 초과 표본 공백도 초기화한다. 보호 latch 진입/유지 자체는 정상적인 연속 저전압 표본의 시간을 초기화하지 않는다. 새 부팅은 새 측정부터 시작한다. millis32bit wrap 시험을 포함한다.

MQTT는 기존 smartbox/telemetry/status에서 warning_schema1, low_warning_active, elapsed_s(0~300), voltage_v11, required_s300을 보낸다. Boolean 전환은 기존 상태 변경 발행처럼 최대1초 주기 제한 안에서 즉시 발행한다. Report interval300초를 기다려 경고를 늦추지 않는다. 실제 shutdown voltage/dwell도 내보내 경고와 구동 보호의 설정값을 읽기 구분한다. Broker ACL이나 계정은 변경하지 않는다.

## NAS 알림 판정

[감시기](../ops/power_monitor.py)는 source freshness/유효성 뒤 동일 telemetry cohort의 경고 증명을 검사한다. Active1은 elapsed300 및 현재11.0V 이하와 일치해야 한다.300초 미만의 마지막 표본을 반복 조회해5분이 경과했다고 계산하지 않는다. 기존 상태 코드1~4의 변경만으로 저전압 알림을 내보내지 않는다. 이미 구형 정책으로 queue에 남은 LOW_BATTERY도 새 active 증명이 없으면 보내지 않는다.

원본 source/센서/통신 오류 경고는 유지한다. 경고 계약이 없는 구형 자료는 WARNING_POLICY_UNAVAILABLE로 구분하며 임의로 저전압으로 간주하지 않는다.11.0V 초과/무효·공백 때 battery warning은 꺼져도 보호 해제는 별도12V/5분 조건을 따른다. RECOVERED 알림 문구도 보호 해제 확인과 구분한다. NAS polling60초로 인해 실제 owner 알림은 장치 qualification 이후 다음 감시 주기에 전달될 수 있다.

## 운영 교체 계획과 권한 경계

[고정 updater](../ops/update_power_monitor.py)는 현재 monitor source/새 source/unit의 exact hash와 root 소유 regular file/고정 경로를 확인한다. Config/계정/token/unit/다른 서비스는 수정하지 않는다. 새 warning-schema telemetry가 저장된 것을 먼저 확인한 뒤 해당 monitor만 stop→원본 code/state 비공개 보존→atomic 교체→start한다. Fresh observed_at와 warning_policy_schema1을 모두 확인해야 적용을 수용한다. 실패하면 원본 code/state/기존 서비스 상태로 복원한다.

현재 SSH sudo는 거절되므로 이미 인증된 DSM 관리자 UI의 비활성 수동 작업에 리뷰된 code bytes/hash를 고정해 실행하는 경로를 준비한다. 새 SSH키·sudo 권한·계정·일반 shell endpoint는 만들지 않는다. DSM이 관리자 재인증을 요구하면 그 입력 단계만 소유자에게 맡긴다. 기존 Wake PC 작업은 수정하지 않는다.

## 검증과 수용 경계

- MCU 새4개 RED→GREEN:299초/300초,11V equality, dip/invalid/gap reset, stale snapshot, protection calls, wrap. 전체 native65 PASS.
- NAS 새9개 중8개 RED→GREEN:조기 모드 경고/같은299초 frame 반복/qualified1회 경고/회복/부족·모순 계약/cohort/오래된 queue/오류 경고 보존.
- Updater6개 RED→GREEN:정확한 code 교체/idempotence,잘못된 원본 거절,stop/start/post-check 실패 복구,구형 worker state를 새 정책으로 오인하지 않음.
- Python144 total/135 executed PASS/9 opt-in 환경 skip, 두 C6 build PASS. 독립 리뷰 지적 없음, 별도로84 power tests PASS.
- 실제0.2.4 설치, 새 warning payload/저장, NAS 교체/서비스 수용은 아직 별도다. 실물 배터리를 방전시켜 경고를 만들거나 테스트 알림을 운영으로 보내지 않는다.

## 실제 펌웨어 설치 수용과 NAS 인증 대기

PR23 headd986764587cc47dfae93b2b8ed68207077eb3975는 CI41782317800 성공 후 기존 관리자용 병합 경로로 main de3aa1a3cd7f9313ff8d8d23a408eae68305eec8에 병합했다. 보안 규칙은 변경하지 않았다. 정상CI/SFTP 게시41782840664와 별도 인증 다운로드41783350078는 모두PASS했다. 운영0.2.4-gde3aa1a/1419664B/SHA256e12e1b3413c683e9758c54ed9aa3375e905b87411879457fb70573107062d227다.

인증 HA에서 새0.2.3 진단12.56805V/valid1/fault0/IDLE/NORMAL/age145ms/idle0.3mA/OTAready1을 확인하고 exclusive intent 뒤15:08:27KST 일반 trigger_ota를QoS0/non-retained로1회 발행했다. 이후 새0.2.4/새 uptime11322→120297ms 이상, 센서 정상/IDLE/NORMAL, 자동 manifest200/UP_TO_DATE를 확인했다. 실제 장치 wire warning_schema1/active0/elapsed0/voltage11/required300 및 shutdown voltage11/dwell3을 읽었다. 새 경고 조건의 실제 코드·설정 전송은 수용한다. 물리적으로 방전시켜5분 경고를 만든 시험은 하지 않았다.

**NAS owner 알림 규칙 교체는 아직 미완료다.** 기존 root monitor는 아직 구형 코드98102212로 간주하며 새 서비스 실행 증거가 없다. root 권한을 추가하지 않고 기존 인증 DSM에서 비활성 수동 작업을 준비했으나 NAS 자체가 작업 저장에 DSM 계정 password를 요구했다. 해당 인증 창은 소유자에게 handoff했다. Task는 아직 저장/실행을 확인하지 않았고 monitor/service/config는 바꾸지 않았다. 후보 공개 source 파일만 웹 루트 밖 docker 공유 폴더에 upload했다.

기존 Task Scheduler 입력 상한8192 때문에 처음 embedded payload12280은 UI 검증에서 거절되어 생성되지 않았다. 고정 updater는 캡처된 bytes로 유지하고 candidate source는 고정 경로/O_NOFOLLOW/크기/hash 검사 뒤에만 사용하는5965byte 작업으로 준비했다. 최종 taskSHA919b7d9ac74730edc89d6b60ef1aaaad42d736ac62bea61fb27ef6ed8fb726fd. Candidate/new monitorSHA1d1d900b2b7e4c8dfb843115759db7063e870b4841db4a0b99ff7a4efedcd8f2, updaterSHA5191c405973e84d4726ea742fe0d64b054cb4f2349ce9896b79dd5ccb0f41188, expected oldSHA98102212eb54e86f149b3e0c3aa0792524a5876cd24dae756f6fca2c307df580, expected unitSHA332a96d15f39853748ce49b274db623ccbfec81558478cfb711a205e0ca0ea70.

### 인증 뒤 남은 실행 순서

1. 기존 NAS password 제출이 끝나 새 SmartBox warning11V300s20261104 task가 **비활성/root**로 저장된 것을 확인한다. 기존 Wake PC 작업은 건드리지 않는다.
2. 준비된 task의 codeSHA와 candidate source/기존 program/unit pin을 대조하고 해당 task만 수동1회 실행한다. Firmware 재설치는 필요하지 않다. Probe가 새 warning-schema cohort를 읽어야 service 교체가 진행된다.
3. File Station docker 공유의 smartbox-warning-policy-20261104-result.json을 읽는다. updated/already_current, 새SHA/config 보존/service_active/current status를 확인한다. 실패이면 error_type/안전 reason과 원본 보존/rollback을 먼저 확인하고 무작정 반복하지 않는다.
4. 새 worker warning_policy_schema1/신선한 observed_at과 새 경고 flag 수용을 확인한 뒤 NAS 알림 조건까지 완료로 기록한다. 현재 이 단계는 미수행이며 장치 펌웨어 적용과 혼동하지 않는다.

## NAS 알림 정책 실제 적용 완료

이후 소유자의 DSM 재인증이 완료되어 준비한 작업이 비활성/root로 저장됐다. 저장 codeSHA919b7d9를 대조한 뒤17:13:54KST1회 수동 실행했다. 실제 결과 파일에서 updatedtrue/service_activetrue/configuration_preservedtrue, 새 monitorSHA1d1d900b2b7e4c8dfb843115759db7063e870b4841db4a0b99ff7a4efedcd8f2 및 currentNORMAL/battery_warning_activefalse/policy11V300초를 읽었다. Fresh worker policy marker 검사를 통과한 적용을 수용한다. Password 대기/미적용 문구는 당시 기록이다.

이 정책은 이후 실제 설치된0.2.5-g945e031에서도 warning schema1/11V300초/active0으로 유지된다.0.2.5의 sensor recheck는 battery warning이나 genuine low hold와 별개다. 작업은 비활성으로 남기고 자동 예약 실행은 켜지 않았다. 기존 Wake PC 및 config/계정/다른 서비스는 보존했다. 실제 방전이나 fake positive 경고를 만들지는 않았다.
