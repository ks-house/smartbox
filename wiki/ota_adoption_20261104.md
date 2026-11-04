# SmartBox0.2.3 OTA 설치 수용 — 2026-11-04

## 설치된 이미지와 요청 범위

소유자가 OTA 업데이트를 요청하고 로컬 브라우저 접근을 허용했다. 기존 Chrome HA 세션으로 최신 무구동 전원 진단을 확인한 뒤 일반 trigger_ota를 사용했다. 설치 대상은 운영main1c093943108a04fdc01d13cc7eb5fed038f98f89, version0.2.3-g1c09394,1418992B,SHA256d0e788cbf94f525d7534a25dfc9e98897a79957c8606dcf19a7907853c897011다. USB 후보4f3d7b3와 구분한다.

수행한 명시 요청은 일반OTA2회, 읽기 진단4회다. 강제OTA/재부팅/개폐 명령은0회다. 각 일반OTA는 exclusive intent 뒤 QoS0/non-retained로1회씩 보냈다. 첫 시도의 확정 실패/기존 앱 정상 복귀와 origin 재검증 후 별도 두 번째 intent를 만들었으며 응답 불확실 상태에서 재전송하지 않았다.

## 첫 시도 — 확정된 스트림 실패

14:01:32KST 발행 전 non-retained0.2.2 진단:13.30004V,IDLE/NORMAL/valid1/fault0, sample age145ms, motor0.6mA, normal OTA ready1/pendingfalse. 실제 IDLE→OTA_UPDATING 이벤트를 받았다. 이후 manifest200/phaseFIRMWARE/attempts2로 진행했다. 이 counter에는 이전404 실패1회가 포함되며 이번 발행2회를 뜻하지 않는다.

14:04:10KST HA state history와 fresh telemetry는 FAILED/FIRMWARE/updater_error6, 같은0.2.2/같은 boot/IDLE 복귀를 보인다. installed Arduino Update.h와 Updater.cpp의6은 stream read timeout or error다. 당시 전원은13.0V대/센서 정상, OTA 중 free_heap142508/RSSI-48, 복귀 후 약206k였다. 배터리 보호나 이미지 공간 오류로 단정하지 않는다. 중단 위치/수신 바이트/특정 네트워크 원인은 노출되지 않아 확정하지 못했다.

Origin 읽기 전용41777207680는 정확한 version/1418992B/SHA 및 TLS, 인증4건/거절8건을 재검증했다. 같은 서버 이미지가 유효했음을 확인한 뒤 재시도를 허용했다. 서비스·설정·권한·펌웨어 파일은 이 사이 바꾸지 않았다.

## 두 번째 시도 — 실제 새 부팅 수용

새 non-retained 진단13.02004V/IDLE/NORMAL/valid1/age131ms/motor0.2mA/normal OTA ready1와 이전 실패 종료를 확인했다.14:08:39KST 발행 후 다시 OTA_UPDATING에 들어갔다.14:11:11~12KST HA reconnect/state와 새 non-retained0.2.3-g1c09394 telemetry가 도착했다.

새 boot uptime11381→30064→60100→77297→90165→120214ms, 센서valid1/fault0,IDLE/NORMAL 및0.1~0.5mA idle branch를 관측했다. 마지막 표본12.74002V/age94ms, since-boot min12.52003V, actuation_allowed1. 새 boot 자체의 자동 manifest 확인은 HTTP200/UP_TO_DATE로 완료됐다. HA 기기 정보도 실제0.2.3-g1c09394를 표시했다. 기존uptime 약6.16millionms 대비 새 부팅과100초 이상 신선한 연속 표본을 확인했으며 장기SLO/전원 재투입 시험으로 확대하지 않는다.

## 11V 정책과 남은 확인 범위

설치된 버전은 [11.0V 이하 보호와 legacy 저장값 전환](low_voltage_threshold_20261104.md)을 포함한다. 새 ConfigManager의 실제호스트 fixture는 migration/보존/실패/1회 쓰기를 검증했다. 현 MQTT payload는 raw cutoff 값을 내보내지 않으므로 실물 NVS의11.0V를 직접 읽었다고 주장하지 않는다. 실제 저전압 차단점 시험도 수행하지 않았다. 보호 해제12.0V/300초 및 기타 안전 조건은 유지한다.

실물 닫힘·태양광 충전 전류·배터리 용량은 미확인이다. HA의 최근 개폐 소요 시간152.5s는 첫 OTA_UPDATING→IDLE 전이와 일치하므로 물리 개폐 성공으로 해석하지 않는다. 신규 power/owner discovery entity가 여전히 보이지 않는 기존 후보 원인도 별도다. 이번 OTA 설치 gate는 수용하되 충전/물리동작 gate까지 닫지 않는다.

진단 listener는 종료했고 publish editor는 읽기 전용 get_power_diagnostics로 복원했다. 설치 버전 proof는 로컬 screenshot/비공개 sanitized intent/result에 보관한다. NAS Container Manager는 조회만 했고 설정·서비스·보안 권한은 변경하지 않았다. 로컬 native61/default C6 문서정책 검사도PASS, 생성 header를 복원했으며 dummy 산출물은 미배포다.
