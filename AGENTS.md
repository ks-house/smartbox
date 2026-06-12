# 🤖 Development Agent Document (AGENTS.md)

이 문서는 AI 코딩 어시스턴트(Antigravity)와 인간 개발자 간의 공동 작업 기록 및 차기 개발 에이전트를 위한 인계 문서입니다. 본 프로젝트의 아키텍처 의사결정 사항, 이력 및 주의 사항을 관리합니다.

---

## 👥 개발 참여 에이전트 정보

* **개발자/에이전트**: Antigravity (Google DeepMind에서 설계한 고급 AI 코딩 어시스턴트)
* **협업 기간**: 2026년 6월 ~
* **주요 역할**: Git 리포지토리 초기 설정 및 정리, 핵심 빌드 프로파일 검토, 하드웨어 연동 구조 문서화 및 가이드라인 제정.

---

## 🛠️ 작업 이력 요약 (Activity Logs)

### Phase 1: Git 리포지토리 구성 및 1차 배포 (2026-06-13)
* **Git 초기화**: 프로젝트 루트에 Git 저장소를 초기화(`git init`)하고 기본 브랜치를 `main`으로 설정했습니다.
* **불필요한 빌드 파일 필터링 (`.gitignore` 설정)**:
  * PlatformIO의 로컬 빌드 산출물 폴더(`.pio/`, `.pioenvs/`, `.piolibdeps/`) 및 자동 생성되는 임시 구성 파일(`compile_commands.json`, `.cache/`)을 영구 제외 처리했습니다.
  * 드라이버/로컬 머신 임시 디렉토리인 `NVIDIA Corporation/` 폴더 및 빌드 로그를 무시 대상에 추가했습니다.
  * VS Code 개발 편의 파일들을 제외해 리포지토리를 최적화했습니다.
* **원격 업로드**: `https://github.com/ks-house/smartbox.git`를 원격 저장소(`origin`)로 지정하고 첫 커밋을 생성해 푸시를 성공적으로 완료했습니다.

### Phase 2: 기술 문서화 작업 (2026-06-13)
* **[README.md](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/README.md) 작성**: 시스템 핀 맵, 동작 제어 흐름(Mermaid Flowchart 포함), `platformio.ini` 환경 설정을 체계적으로 설명하는 설명서를 작성했습니다.
* **[AGENTS.md](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/AGENTS.md) 작성**: 본 아키텍처 의사결정 및 개발 가이드를 작성했습니다.

---

## 📐 핵심 아키텍처 결정 사항 (Architecture Decisions)

1. **릴레이 이중 전원 제어 설계**:
   * **배경**: 리니어 액추에이터는 정지 상태에서도 내장 회로 및 릴레이의 미세 전류로 인해 대기 전력 소모가 지속될 수 있으며, 오동작으로 인한 과전류 유입 위험이 있습니다.
   * **결정**: 액추에이터 모터 라인의 전단에 1채널 릴레이(`RELAY_MAIN_PIN`)를 위치시켜 대기 모드와 투척 대기 중에는 완전히 12V 공급 전원을 컷오프(Cut-off)시킴으로써 전력 소모량을 0으로 만들고 동작 시에만 전원을 공급하도록 설계했습니다.
2. **ESP32-C6 및 Arduino 3.x 환경 통합**:
   * **결정**: ESP32-C6 보드에 대한 완벽한 호환을 제공하기 위해 표준 espressif32 플랫폼이 아닌, 커뮤니티에서 활발히 관리되는 `pioarduino` 사의 포크 저장소(`https://github.com/pioarduino/platform-espressif32.git`)를 사용하도록 명시했습니다.

---

## 🚨 향후 개발 에이전트를 위한 작업 주의 사항 (Developer & Agent Guidelines)

차기 작업을 수행하는 AI 에이전트나 인간 개발자는 다음 주의 사항을 반드시 확인해야 합니다.

* **하드웨어 단락(Short-Circuit) 방지 가이드**:
  * 2채널 방향 제어 릴레이(`RELAY_DIR_A_PIN`, `RELAY_DIR_B_PIN`)의 오동작 또는 프로그래밍 버그로 인해 두 핀 모두가 동시에 `HIGH`가 될 경우, 모터 컨트롤러 또는 릴레이 결선 방식에 따라 전원 단락 혹은 회로 소손의 위험이 발생할 수 있습니다.
  * 코드 수정 시 두 릴레이 핀이 동시에 `HIGH`로 들어가지 않도록 상호 배타적 제어 코드를 준수해야 합니다.
* **동작 타이밍 최적화**:
  * 현재 사용 중인 액추에이터 스펙(200mm 스트로크, 60mm/s 속도) 기준으로 개폐 시간이 `3.8초`(`ACTUATOR_TIME`)로 세팅되어 있습니다. 하드웨어 스펙이 변경될 경우 `platformio.ini`와 호환되는 이 값을 현실적인 뚜껑 동작 범위에 맞춰 수정해야 합니다.
  * 액추에이터가 멈춘 이후 기계적 진동이 감쇄되고 안전하게 닫힐 수 있도록 쿨타임(`delay(3000)`) 및 릴레이 접점 지연 대기 시간(`delay(100)`)을 유지해야 합니다.
* **자동 생성 파일 커밋 금지**:
  * 펌웨어 수정 후 컴파일 과정에서 `.pio`나 `compile_commands.json`이 재생성될 수 있습니다. 실수로 `git add`에 강제 추가되지 않도록 조심하세요.
