#11V/5분 경고 실행 계획

1. 경고와 모터 보호 분리:기존 보호3초·센서/전류/접점/OTA 보호 유지.
2. MCU 300초 연속 증명과 wire fields/change publication 구현 및 경계 RED→GREEN.
3. NAS proof/cohort validation, state-only 경고 제거, 이전 queue 정리와 오류 알림 보존.
4. 고정 hash-pinned updater의 서비스 범위/backup/rollback/fresh-worker proof 시험.
5. source/두 C6/전체 Python/독립 review 후 PR/기존CI OTA publication, 정상 전원에서 단일 OTA 수용.
6. 이미 인증된 DSM의 비활성 수동 작업으로 고정 updater 적용. 추가 인증이 나오면 실제 입력 단계만 handoff.
7. 신선한 장치 warning fields·Influx 저장·NAS worker policy marker·기존 config 보존을 수용하고 wiki/issue 갱신. 저전압을 인위적으로 만들거나 실물 움직임을 추가하지 않음.
