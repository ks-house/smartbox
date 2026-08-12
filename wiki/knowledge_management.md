---
title: SmartBox knowledge management
type: governance
project: smartbox
status: active
updated: 2026-08-12
source_of_truth: true
---

# Obsidian·LLM 문서 운영

## 진실 공급원

- 현재 프로젝트 사실: 이 저장소의 source, tests, `wiki/`, 현재형 `docs/`.
- 시점별 기록: `reports/`, `prompts/`, superseded plan 문서.
- 공통 운영 원칙: `E:\knowledge-hub\knwlege-hub`.
- 최종 구현 근거: source와 tests. 문서는 이를 설명하고 찾게 한다.

## 세 가지 사실 축

1. Repository implementation: 현재 checkout에 코드가 있다.
2. Verified evidence: 특정 commit/environment에서 시험을 통과했다.
3. Deployed state: 실제 장치에 설치되어 버전과 동작을 관측했다.

세 축을 “완료”라는 한 단어로 합치지 않는다.

## 문서 유형

- `reference`: 현재 구조와 값.
- `runbook`: 재현 가능한 운영 절차.
- `decision`: 선택과 근거.
- `proposal`: 아직 채택되지 않은 설계.
- `incident`: 특정 시점의 관측과 원인.
- `test`: 조건·artifact·결과.
- `governance`: 문서와 release 운영 규칙.

상태는 `draft`, `proposed`, `active`, `superseded`, `deprecated`를 사용한다. 기존 문서는 수정할 때 점진적으로 frontmatter를 적용하며 metadata만을 위한 대량 변경은 하지 않는다.

## 현재 파일 분류

- `wiki/index.md`, `project_status.md`, `architecture.md`: 현재형 진입점.
- `docs/USER_MANUAL.md`, `DEVELOPMENT_MANUAL.md`, `WIRING_GUIDE.md`: 유지해야 하는 현재형 매뉴얼.
- `docs/*plan.md`, `task.md`, `walkthrough.md`: 완료된 작업 snapshot.
- `reports/`: 날짜/순번별 역사 자료.
- `prompts/`: transcript archive.

역사 자료는 삭제하거나 현재형으로 덮지 않는다. 코드와 어긋나는 경우 상단에 superseded 표시와 현재 문서 링크를 둔다.

## Hub 승격

다음 조건을 만족하는 원칙만 공통 Hub로 승격한다.

- 제품 이름과 GPIO 값을 제거해도 재사용 가능하다.
- 두 프로젝트 이상에 적용되거나 범용 안전·릴리스 패턴이다.
- 원본 path와 기준 commit을 기록할 수 있다.
- 프로젝트별 endpoint, secret, device identity와 배포 결과를 포함하지 않는다.

SmartBox에서 승격할 수 있는 대표 항목은 Active-Low relay 방향 전환 interlock과 actuator가 있는 장치의 pre-OTA hardware isolation이다.

## 작업 종료

1. index와 최근 log를 읽는다.
2. source/test/deployment를 분리해 확인한다.
3. 현재형 문서와 코드 차이를 수정한다.
4. 새 문서를 index에 연결한다.
5. log에 append한다.
6. 상대 링크, 비밀, UTF-8, 모순을 검사한다.
