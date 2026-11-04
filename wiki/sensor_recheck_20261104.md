# 강제 열기 후 순간 범위 초과와 배터리 보호 오분류 — 2026-11-04

## 실제 사건

소유자가 강제 열기를 누른 뒤 상태가 바뀌었다고 신고하고, 보호 모드로 너무 쉽게 들어가지 않도록 수정하라고 요청했다. 에이전트는 추가 개폐/리셋/OTA로 재현하지 않고 기존 HA/Influx 자료를 읽었다.

- HA16:20:55KST 버튼 눌림/OPEN_START,16:20:56 OPENING→BATTERY_LOW_SHUTDOWN.
- Influx collector16:20:57.359KST:valid0/fault9 RANGE, config14751(0x399f)/cal4096, 전압·전류 null.
-16:20:58.345KST:valid1/fault0/12.43603V/0.3mA, 같은 config/cal.
- Fresh 진단0.2.4/uptime264611ms/12.43603V/valid1/age27ms/RECOVERING/remaining178s, since-boot valid minimum12.35202V, battery warning0, 실제 shutdown11V/3s.

이것은11V 이하가3초 지속됐다는 증거가 아니다. BatteryPolicy::sample이 무효 표본 한 번에 voltage latch를 켜고300초 회복을 요구한 경로와 일치한다. RANGE9는 유한한 버스/션트/전류 값의 범위 판정이다.0.2.4는 실패한 raw값을 보존하지 않아 어느 값이 초과했는지, 모터 기동 전류/기계 부하/전압 spike 중 무엇인지는 확정하지 못했다. Collector receipt 시각과 실제 GPIO/motor-on 시각을 혼동하지 않는다. 최근16:18 재연결/새 boot 원인도 현재 자료로 단정하지 않는다.

## 수정0.2.5 후보

- 무효 전원 표본은 그대로 무효로 유지하고 즉시 모든 릴레이를 격리한다. Callback/NVS 작업보다 먼저 차단한다.
- 순간 측정 오류는 새 FSM SENSOR_RECHECK로 구분하고, 그 이유만으로 배터리 persistent hold/NVS marker를 만들지 않는다.
- 유효한 표본이1초 이상 이어져야 IDLE로 돌아간다. 그 전에 새 명령을 허용하지 않으며 무효 표본/2초 초과 gap은 확인 시간을 다시 시작한다. 지속 오류는 계속 격리한다.
- 중단된 구동을 자동 재시작하지 않는다. 이후 자동 감지는 기존 deadlock-clear 계약의10초 연속 부재 후 새 접근을 필요로 한다. 전원 검증을 통과한 새 수동 명령은 별도로 허용한다.
- 실제11V 이하3초 battery hold와12V/300초 회복은 유지하고 센서 재확인으로 genuine hold를 해제하지 않는다. 경고11V/300초도 그대로다.
- 명시 force_close는 논리 IDLE에서도1회 닫기 경로를 시작할 수 있다. 논리 IDLE이 실제 닫힌 위치를 보장하지 않는 점을 반영하며, 전원/센서/과전류/접점/시간/사람 재접근 보호를 유지한다. 반복 명령으로 CLOSING 시간을 연장하지 않는다.

기존 저장 NVS/lid-unknown marker로 인한 보수적 boot hold는 임의로 지우지 않는다. 새 transient 재확인과 legacy boot/진짜 battery hold를 구분한다. 추가 실제 개폐 시험은 하지 않는다.

## 진단 보존과 OTA

RANGE를 여전히 valid power로 취급하지 않는다. Raw bus/shunt/current와 mask1=전압,2=션트,4=전류를 실패 시에도 진단용으로 별도 보존한다. 마지막 오류 code/episode/age와 원시값을 유지해 정상 회복 후에도 원인이 사라지지 않게 한다. 이 값은 현재 정상 전압/전류나 OTA admission으로 재사용하지 않는다.

전원·현재 reading·마지막 fault/count/age는 한 mutex frame에서 직렬화한다. 기존 상태/진단 namespace로 보내므로 broker ACL은 바꾸지 않는다. 관측한 status에 최대형 진단값을 넣은 packet1543B+MQTT48B는 기존2048B 안이었다.

Sensor-fault recovery OTA는 SENSOR_RECHECK에서도 기존 explicit/once/TTL/last-known-voltage 조건으로 유지된다. Normal/forced OTA의 전원 qualification과 final TTL은 유지한다. OTA가 실패/같은 버전으로 돌아오더라도 센서 중단 뒤 자동 재구동을 차단해야 한다.

## 검증

- 신규7개 native 회귀 RED→GREEN:순간 오류 짧은 복귀/genuine low hold 보존/invalid·gap 재시작/즉시 출력 격리와 자동 재시도 차단/IDLE 명시 닫기/재확인 중 명령 거절/raw RANGE 진단.
- 독립 리뷰는 Important1개를 찾았다. Sensor recovery OTA 중 센서가 회복되면 cleanup이 직접 IDLE로 돌아가 자동 재구동 억제를 우회했다. 실제 native 재현 RED 후 무효 표본에서 억제 flag를 미리 세워 cleanup 경로에도 보존했다. 회귀 GREEN, 전체 native73 PASS.
- 실제 updater21개 시나리오와 Python144 total/135 executed PASS/9 환경 skip, 두 C6 build PASS. Protected1Hz sampling의 real low3초를 검증하도록 fixture의 유효 관측 구간을 충분히 유지했다.
- 생성 header는 복원했고 dummy image는 미배포다. 실제 새 버전 설치·boot·진단 readback 및 소유자의 다음 자연스러운 개폐 결과는 별도 gate다. ROOT NAS warning monitor1d1 후보/재인증 대기는 변경하지 않았다.
