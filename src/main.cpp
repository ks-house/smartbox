#include <Arduino.h>

// ==========================================
// 1. 핀 맵핑 (ESP32-C6에 선을 연결할 실제 핀 번호로 수정하세요)
// ==========================================
const int TRIG_PIN = 4;        // 초음파 센서 TRIG
const int ECHO_PIN = 5;        // 초음파 센서 ECHO
const int RELAY_MAIN_PIN = 6;  // 1채널 릴레이 (12V 메인 전원 컷오프)
const int RELAY_DIR_A_PIN = 7; // 2채널 릴레이 IN1 (뚜껑 열기 방향)
const int RELAY_DIR_B_PIN = 8; // 2채널 릴레이 IN2 (뚜껑 닫기 방향)

// ==========================================
// 2. 작동 설정값
// ==========================================
const int DIST_THRESHOLD = 50;            // 사람이 다가왔다고 판단할 거리 (cm)
const unsigned long ACTUATOR_TIME = 3800; // 액추에이터 작동 시간 (밀리초)
// 💡 계산: 200mm 스트로크 / 60mm/s 속도 = 약 3.3초. 끝까지 닿게 3.8초로 넉넉히
// 설정 (끝에 닿으면 내장 리밋 스위치로 자동 정지됨)
const unsigned long WAIT_TIME = 5000; // 뚜껑이 열린 채로 대기하는 시간 (5초)

// ==========================================
// 3. 초기 설정 (Setup)
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // 1채널 릴레이 초기 상태: OFF (INPUT 모드로 전환하여 3.3V 풀업 문제 회피)
  pinMode(RELAY_MAIN_PIN, INPUT);
  
  pinMode(RELAY_DIR_A_PIN, OUTPUT);
  pinMode(RELAY_DIR_B_PIN, OUTPUT);

  // 부팅 시 모든 릴레이 전원 강제 차단 (안전 제일)
  digitalWrite(RELAY_DIR_A_PIN, HIGH);
  digitalWrite(RELAY_DIR_B_PIN, HIGH);

  Serial.println("스마트 수거함 시스템 부팅 완료. 감지 대기 중...");
}

// ==========================================
// 4. 초음파 거리 측정 함수
// ==========================================
long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // ECHO 핀에서 신호가 돌아오는 시간 측정 (타임아웃 30ms 설정하여 블로킹 방지)
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0)
    return 999;                  // 측정 실패 또는 너무 멀 때
  return (duration * 0.034) / 2; // cm 단위로 환산
}

// ==========================================
// 5. 메인 무한 루프
// ==========================================
void loop() {
  long dist = getDistance();
  Serial.printf("현재 거리: %d cm\n", dist);

  // 사람이 설정 거리(50cm) 이내로 다가왔을 때
  if (dist > 0 && dist < DIST_THRESHOLD) {
    Serial.println(
        "🏃‍♂️ 사람 감지됨! 뚜껑 개방 시퀀스 시작...");

    // [STEP 1] 1채널 릴레이 ON: 액추에이터 모터 쪽으로 12V 메인 전원 길 열어주기
    pinMode(RELAY_MAIN_PIN, OUTPUT);
    digitalWrite(RELAY_MAIN_PIN, LOW);
    delay(100); // 릴레이 내부의 기계적 접점이 찰칵 하고 붙을 때까지 잠깐 대기

    // [STEP 2] 2채널 릴레이 제어: 뚜껑 열기 (정회전)
    digitalWrite(RELAY_DIR_A_PIN, LOW);
    digitalWrite(RELAY_DIR_B_PIN, HIGH);
    Serial.println(">> 뚜껑 열리는 중...");
    delay(ACTUATOR_TIME);

    // [STEP 3] 뚜껑 다 열림 ➔ 쓰레기 버릴 동안 모든 전력 차단 (배터리 절약)
    digitalWrite(RELAY_DIR_A_PIN, HIGH);
    digitalWrite(RELAY_DIR_B_PIN, HIGH);
    pinMode(RELAY_MAIN_PIN, INPUT); // 1채널 릴레이 OFF (INPUT 모드로 전환)
    Serial.println(">> 쓰레기 투척 대기 (5초간 정지 및 전력 차단)");
    delay(WAIT_TIME);

    // [STEP 4] 1채널 릴레이 다시 ON ➔ 뚜껑 닫기 (역회전)
    pinMode(RELAY_MAIN_PIN, OUTPUT);
    digitalWrite(RELAY_MAIN_PIN, LOW);
    delay(100);
    digitalWrite(RELAY_DIR_A_PIN, HIGH);
    digitalWrite(RELAY_DIR_B_PIN, LOW);
    Serial.println(">> 뚜껑 닫히는 중...");
    delay(ACTUATOR_TIME);

    // [STEP 5] 완료: 다시 완전 절전 모드로
    digitalWrite(RELAY_DIR_A_PIN, HIGH);
    digitalWrite(RELAY_DIR_B_PIN, HIGH);
    pinMode(RELAY_MAIN_PIN, INPUT); // 1채널 릴레이 OFF (INPUT 모드로 전환)
    Serial.println("✅ 개폐 시퀀스 완료. 다시 감지 모드로 돌아갑니다.\n");

    // 사람이 완전히 물러날 때까지 약간의 쿨타임 대기 (중복 감지 방지)
    delay(3000);
  }

  delay(500); // 0.5초마다 거리 측정
}