# 🤖 스마트 자동 수거함 TDD 및 비동기 웹/네트워크 구현 지침 (2차 프롬프트)

본 문서는 시니어 S/W 아키텍트의 설계 심의를 통과한 구현 계획서([docs/implementation_plan.md](../docs/implementation_plan.md)) 및 작업 리스트([docs/task.md](../docs/task.md))를 기반으로, 실제 프로그래밍을 진행할 AI 코딩 에이전트에게 제공하는 종합 구현 지침 프롬프트입니다.

---

## 1. 개발 핵심 목표 및 철학
1. **"Hardware Safety First"**: 12V 릴레이 H-브릿지 쇼트와 초기화 글리치로 인한 오작동을 차단하는 인터락 시퀀스를 최우선으로 준수합니다.
2. **테스트 가능 설계 (TDD)**: 하드웨어 결선을 모사한 추상화 레이어(`HardwareInterface`, `MockHardware`)를 적극 도입하여, 실제 보드가 없어도 PC 로컬 환경(`native`)에서 FSM 논리와 인터락 타이머가 유닛 테스트를 통과(RED-GREEN-REFACTOR)하도록 개발합니다.
3. **비동기 멀티태스킹**: 와이파이 재연결 지연이나 스마트싱스 HTTP REST API 호출 대기 시간이 수거함의 핵심 초음파 및 모터 감시 제어 주기(50ms)를 블로킹하지 않도록 RTOS 태스크 및 이벤트 큐로 완벽히 분리 구현합니다.

---

## 2. 주요 구현 사양

### ① Hardware Abstraction Layer (HAL)
- `HardwareInterface` 추상 클래스 설계 및 PC 테스트를 위한 `MockHardware` 더블 작성.
- 실제 보드용 `Esp32Hardware`는 Arduino Core 및 `Adafruit_INA219` I2C 센서 API 연동.

### ② Core FSM (논블로킹 8개 상태 및 비상/저전압 보호)
- 상태 정의: `IDLE`, `OPEN_START`, `OPENING`, `HOLD`, `CLOSE_START`, `CLOSING`, `EMERGENCY_STOP`, `BATTERY_LOW_SHUTDOWN`.
- 부팅 글리치 가드: `setup()` 진입 시 릴레이 제어 핀(`GPIO 6, 7, 8`)의 `pinMode(PIN, INPUT)` 지정 및 `digitalWrite` 선제 처리.
- 릴레이 인터락: 방향 제어 핀 동시 활성화(`LOW`) 금지. 방향 전환 시 메인 전원을 끄고 **100ms 대기 가드** 시간 제공.
- 과전류 정지: 기동 전류(300ms) 무시 후 800mA 초과 시 즉시 비상 정지 상태 전이 및 릴레이 고임피던스 해제.
- 저전압 방전 방어: 배터리 전압 11.3V 이하 3초 유지 시 자동 뚜껑 강제 개방 후 전원 핀 `INPUT` 전환 및 동결.
- 노이즈 필터: 50ms 주기 초음파 거리 센서 데이터에 대한 5개 샘플 중간값 필터(Median Filter) 구현.

### ③ 실시간 설정 변경 및 이벤트 리포트
- 감지 거리, HOLD 시간, 과전류 임계값 설정값을 담은 `BoxConfig` 구조체 구현.
- 설정 수정 시 `Preferences`를 활용한 비휘발성 Flash 저장 연동.
- FSM 상태 변경 발생 시 호출되는 이벤트 콜백 인터페이스(`StateChangeCallback`) 구현.

### ④ 비동기 무선 연결 및 SmartThings 연동
- WiFiManager를 통한 백그라운드 Wi-Fi 비동기 감시 및 재연결.
- FSM 상태 콜백 수신 시 이벤트를 FreeRTOS 큐에 삽입하고, 백그라운드 태스크에서 비동기로 SmartThings Webhook API 전송.

### ⑤ 비동기 웹 관리자 대시보드
- `ESPAsyncWebServer` 및 WebSocket 프로토콜 채택.
- 새로고침 없는 센서 데이터(cm, mA, V, FSM State) 실시간 WebSocket 전송.
- 웹 페이지 내 실시간 세팅 조절 UI 연동 및 원격 제어 명령(강제 개방, 비상 해제) 처리 인터페이스 연결.

---

## 3. 이행 순서 및 참고 문서
- 구현 작업은 반드시 **[docs/task.md](../docs/task.md)**에 상세 수립된 **1단계부터 12단계까지의 순서**를 엄격히 준수하며 TDD 방식으로 진행하십시오.
- 하드웨어 핀 맵 및 물리 결선 요령은 **[DEVELOPMENT_MANUAL.md](../docs/DEVELOPMENT_MANUAL.md)** 및 **[WIRING_GUIDE.md](../docs/WIRING_GUIDE.md)**를 참고하십시오.
