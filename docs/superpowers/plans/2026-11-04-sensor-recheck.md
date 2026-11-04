# 순간 측정 범위 오류의 안전한 재확인 계획

1. 사용자 동작의 HA state/Influx fault9/정상 회복을 읽기만 하고 보존한다.
2. 무효 표본 즉시 출력 차단은 유지하되 persistent voltage hold와 transient sensor recheck를 분리한다.
3. 유효1초/gap reset/수동 재요청/자동 부재10초 재무장/진짜 저전압 hold 보존을 native로 재현한다.
4. Raw range evidence와 일관된 진단 frame을 추가하고 OTA return 경로의 재구동 억제를 검증한다.
5. 전체 native/Python/실제 updater/양쪽 C6 및1회 독립 리뷰 후 중요한 문제를 고친다.
6. 기존 CI 게시·인증 다운로드·정상 전원에서 단일 OTA 후 실제 새 boot/조건 readback 확인. 추가 무인 개폐로 회귀 시험하지 않는다.
7. NAS warning monitor 기존 staged code와 password handoff는 유지하며 해당 배포를 완료했다고 오인하지 않는다.
