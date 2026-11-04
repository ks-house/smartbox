# OTA 설치 후 충전용 닫기 — 2026-11-04

소유자가 새 버전 설치 후 즉시 뚜껑 닫기를 요청했다. 기존 인증 HA MQTT UI를 사용했다. Fresh non-retained0.2.3-g1c09394/uptime862346ms/12.72404V/valid1/fault0/age324ms/motor0.4mA를 확인했다. FSM은BATTERY_LOW_SHUTDOWN, 전원은RECOVERING/remaining191s였고 charging_close_used0이었다. 일반 actuation0이므로 보호를 유지하는 명시 close_for_charging을 선택했다. 보호가 다시 latch된 원인은 이번 조회로 확정하지 않았다. 이때 since-boot min12.52003V였으므로 새11V 저전압 사건으로 단정하지 않는다.

Exclusive intent와90초 TTL/32hex request ID 뒤14:26:24KST QoS0/non-retained로1회 발행했다. 별도 개방/긴급정지 해제/재부팅/OTA/설정·권한 변경은0회다. 잘못된/중복 JSON은 발행 전에 정리했고 명령 재전송은 하지 않았다.

실제 non-retained admission uptime924932ms/12.99203V/age1ms에서 close_state1/used1을 보였다. 구동 batch4개는925395..928395ms,12.41602~12.50499V/783~955.7mA를 기록했다. Terminal uptime929396ms는 close_state2 STROKE_FINISHED/used1,12.2342V/969.9mA/age665ms였다. 이 마지막 전류 표본은 종료 직전의 지연 측정값이며, 종료 후 구동 지속으로 해석하지 않는다. Controller timestamps 차이4464ms는 접점/표본/발행 시각을 포함하므로 실물 motor-on 시간으로 단정하지 않는다.

종료 뒤uptime930400ms에서12.74404V/0.3mA, 추가 읽기 진단994441ms에서12.75605V/0.4mA,1111092ms에서12.83203V/0.3mA/age164ms/valid1/fault0를 확인했다. close_state2/used1이 유지됐고 저전압3·센서4·과전류5·만료6·취소7 종료 코드는 없었다. 보호를 유지하며 RECOVERING이300→236→119s로 진행했다. 이번 부팅의 충전용 닫기 시도는 소모됐다.

**닫기 구동의 정상 종료와 부하/정지 전류를 수용한다. 실물 완전 닫힘·위치·태양광 충전 전류는 미확인이다.** 제한 스위치/위치 센서가 없는 이 경로에서 code2는 시간 종료이지 실제 폐쇄 위치 확인이 아니다. 측정 전압이 기존11.3V보다도 높았으므로 이번 결과만으로11V 차단점의 실물 검증을 주장하지 않는다. 일반 저전압 fail-open/복구 후 자동 감지는 기존 계약대로 남아 있다.

읽기 진단은3회다. JSON 표시를 켜 새 완료 payload의 code2/used1/current0.3mA를 스크린샷으로 저장했고 listener를 종료했다. Publish editor는 get_power_diagnostics로 복원했다. 운영 설정 변경이나 추가 motion은 하지 않았다. Private intent/result와 screenshot은 Git 밖에 둔다. Local native61/default C6 문서정책 검사PASS, 생성 header 복원 및 dummy 산출물 미배포.
