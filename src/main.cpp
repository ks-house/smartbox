#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <WebServer.h>

// ==========================================
// 1. 하드웨어 핀 맵핑 (GPIO Assignments)
// ==========================================
const int TRIG_PIN = 4;        // 초음파 센서 TRIG
const int ECHO_PIN = 5;        // 초음파 센서 ECHO
const int RELAY_MAIN_PIN = 6;  // 1채널 릴레이 (12V 메인 전원 컷오프)
const int RELAY_DIR_A_PIN = 7; // 2채널 릴레이 IN1 (액추에이터 정회전/개방)
const int RELAY_DIR_B_PIN = 8; // 2채널 릴레이 IN2 (액추에이터 역회전/폐쇄)

const int I2C_SDA = 22;        // INA219 SDA
const int I2C_SCL = 23;        // INA219 SCL

// ==========================================
// 2. 타이밍 및 감지 임계값 (Settings)
// ==========================================
const int DIST_THRESHOLD = 50;            // 감지 범위 (cm)
const unsigned long ACTUATOR_TIME = 3800; // 액추에이터 구동 제한 시간 (ms)
const unsigned long WAIT_TIME = 5000;     // 개방 유지 시간 (ms)
const unsigned long COOLDOWN_TIME = 3000; // 닫힘 완료 후 중복 감지 방지 쿨다운 (ms)

const float VOLTAGE_SHUTDOWN_LIMIT = 11.3; // 배터리 저전압 가드값 (V)
const float CURRENT_STALL_LIMIT = 800.0;   // 모터 과전류 임계값 (mA)

// ==========================================
// 3. 글로벌 상태 및 센서 변수
// ==========================================
Adafruit_INA219 ina219;
bool hasINA219 = false;

WebServer server(80);
const char* AP_SSID = "SmartBox-WiFi";
const char* AP_PASS = ""; // 개방형 AP (비밀번호 없음)

enum State {
  STATE_IDLE,
  STATE_OPEN_START,
  STATE_OPENING,
  STATE_HOLD,
  STATE_CLOSE_START,
  STATE_CLOSING,
  STATE_EMERGENCY_STOP,
  STATE_BATTERY_LOW_SHUTDOWN
};

State currentState = STATE_IDLE;
unsigned long stateTimer = 0;
unsigned long sensorTimer = 0;
unsigned long cooldownTimer = 0;
bool isCooldown = false;

// 초음파 중간값 필터 버퍼
const int FILTER_SIZE = 5;
long distBuffer[FILTER_SIZE] = {999, 999, 999, 999, 999};
int filterIdx = 0;

// 모니터링 변수 (실시간 반영)
float batteryVoltage = 12.0;
float motorCurrent = 0.0;
long currentDistance = 999;

// ==========================================
// 4. 웹 대시보드 HTML/CSS/JS (Vibrant Dark Theme)
// ==========================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>SmartBox Control Panel</title>
  <link href='https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap' rel='stylesheet'>
  <style>
    :root {
      --bg-color: #0b0c10;
      --card-bg: rgba(255, 255, 255, 0.03);
      --card-border: rgba(255, 255, 255, 0.06);
      --text-color: #f1f3f9;
      --text-muted: #858a9d;
      --primary: #5856d6;
      --success: #10b981;
      --danger: #ef4444;
      --warning: #f59e0b;
      --info: #0a84ff;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Outfit', sans-serif; }
    body { background-color: var(--bg-color); color: var(--text-color); padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    .container { width: 100%; max-width: 460px; }
    .header { text-align: center; margin-bottom: 25px; }
    .header h1 { font-size: 2.2rem; font-weight: 800; background: linear-gradient(135deg, #a5b4fc, #5856d6); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
    .header p { color: var(--text-muted); font-size: 0.95rem; margin-top: 5px; }
    .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 24px; padding: 25px; backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px); box-shadow: 0 30px 60px rgba(0,0,0,0.4); margin-bottom: 20px; position: relative; overflow: hidden; transition: all 0.3s; }
    .card::before { content: ''; position: absolute; top: 0; left: 0; width: 100%; height: 4px; background: var(--primary); }
    .status-card.state-idle::before { background: var(--success); }
    .status-card.state-running::before { background: var(--info); }
    .status-card.state-emergency::before { background: var(--danger); }
    .status-card.state-battery::before { background: var(--warning); }
    .status-badge { display: inline-flex; align-items: center; padding: 6px 14px; border-radius: 30px; font-weight: 600; font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.5px; }
    .status-badge.idle { background: rgba(16, 185, 129, 0.15); color: var(--success); }
    .status-badge.running { background: rgba(10, 132, 255, 0.15); color: var(--info); }
    .status-badge.emergency { background: rgba(239, 68, 68, 0.15); color: var(--danger); animation: pulse 1.5s infinite; }
    .status-badge.battery { background: rgba(245, 158, 11, 0.15); color: var(--warning); }
    @keyframes pulse { 0% { opacity: 0.6; } 50% { opacity: 1; } 100% { opacity: 0.6; } }
    .state-title { font-size: 1.6rem; font-weight: 600; margin: 15px 0 5px 0; }
    .state-desc { color: var(--text-muted); font-size: 0.9rem; margin-bottom: 20px; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
    .metric-box { background: rgba(255, 255, 255, 0.01); border: 1px solid var(--card-border); border-radius: 16px; padding: 15px; text-align: center; }
    .metric-label { font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; margin-bottom: 5px; }
    .metric-value { font-size: 1.35rem; font-weight: 600; }
    .btn { display: block; width: 100%; border: none; padding: 16px; border-radius: 16px; font-weight: 600; font-size: 0.95rem; cursor: pointer; transition: all 0.2s; text-align: center; }
    .btn-danger { background: var(--danger); color: white; box-shadow: 0 8px 20px rgba(239, 68, 68, 0.2); }
    .btn-danger:hover { background: #dc2626; transform: translateY(-2px); }
    .btn-disabled { background: rgba(255,255,255,0.04); color: var(--text-muted); cursor: not-allowed; }
  </style>
</head>
<body>
  <div class='container'>
    <div class='header'>
      <h1>SmartBox ☀️</h1>
      <p>Autonomous Solar Trash Collector</p>
    </div>
    <div class='card status-card' id='statusCard'>
      <div>
        <span class='status-badge' id='statusBadge'>Connecting...</span>
      </div>
      <h2 class='state-title' id='stateTitle'>Connecting...</h2>
      <p class='state-desc' id='stateDesc'>Please wait while connecting to the smart controller.</p>
      
      <div class='grid'>
        <div class='metric-box'>
          <div class='metric-label'>🔋 Battery Voltage</div>
          <div class='metric-value' id='valBattery'>-- V</div>
        </div>
        <div class='metric-box'>
          <div class='metric-label'>⚡ Actuator Current</div>
          <div class='metric-value' id='valCurrent'>-- mA</div>
        </div>
        <div class='metric-box'>
          <div class='metric-label'>📏 Human Distance</div>
          <div class='metric-value' id='valDistance'>-- cm</div>
        </div>
        <div class='metric-box'>
          <div class='metric-label'>⏱️ Active Time</div>
          <div class='metric-value' id='valTime'>-- s</div>
        </div>
      </div>
    </div>
    <button class='btn btn-disabled' id='btnRelease' disabled onclick='releaseEmergency()'>Release Emergency Lock</button>
  </div>

  <script>
    async function updateStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
        const badge = document.getElementById('statusBadge');
        const title = document.getElementById('stateTitle');
        const desc = document.getElementById('stateDesc');
        const card = document.getElementById('statusCard');
        const btn = document.getElementById('btnRelease');
        
        document.getElementById('valBattery').innerText = data.battery.toFixed(2) + ' V';
        document.getElementById('valCurrent').innerText = data.current.toFixed(0) + ' mA';
        document.getElementById('valDistance').innerText = data.distance + ' cm';
        document.getElementById('valTime').innerText = (data.time / 1000).toFixed(1) + ' s';
        
        card.className = 'card status-card';
        btn.className = 'btn btn-disabled';
        btn.disabled = true;
        
        switch(data.state) {
          case 'IDLE':
            badge.className = 'status-badge idle';
            badge.innerText = 'Standby';
            title.innerText = 'Monitoring Ready';
            desc.innerText = 'System is ready and waiting for human approach.';
            card.classList.add('state-idle');
            break;
          case 'OPEN_START':
          case 'OPENING':
            badge.className = 'status-badge running';
            badge.innerText = 'Opening';
            title.innerText = 'Opening Lid';
            desc.innerText = 'Linear actuator is extending to push the lid open.';
            card.classList.add('state-running');
            break;
          case 'HOLD':
            badge.className = 'status-badge running';
            badge.innerText = 'Open';
            title.innerText = 'Lid Held Open';
            desc.innerText = 'Trash bin is open. Ready for waste deposition.';
            card.classList.add('state-running');
            break;
          case 'CLOSE_START':
          case 'CLOSING':
            badge.className = 'status-badge running';
            badge.innerText = 'Closing';
            title.innerText = 'Closing Lid';
            desc.innerText = 'Lid is lowering. Interlock and human detection active.';
            card.classList.add('state-running');
            break;
          case 'EMERGENCY_STOP':
            badge.className = 'status-badge emergency';
            badge.innerText = 'Emergency';
            title.innerText = 'System Locked';
            desc.innerText = 'Stall current detected or manual stop triggered!';
            card.classList.add('state-emergency');
            btn.className = 'btn btn-danger';
            btn.disabled = false;
            break;
          case 'BATTERY_LOW_SHUTDOWN':
            badge.className = 'status-badge battery';
            badge.innerText = 'Low Battery';
            title.innerText = 'Lid Locked Open';
            desc.innerText = 'Battery voltage dropped below 11.3V. Relays isolated.';
            card.classList.add('state-battery');
            break;
        }
      } catch(e) {
        console.error(e);
      }
    }
    
    async function releaseEmergency() {
      if(confirm('Are you sure you want to release emergency lock?')) {
        const btn = document.getElementById('btnRelease');
        btn.className = 'btn btn-disabled';
        btn.disabled = true;
        await fetch('/api/release');
        updateStatus();
      }
    }
    
    setInterval(updateStatus, 1000);
    updateStatus();
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// 5. 하드웨어 릴레이 안전 가드 API
// ==========================================

// 릴레이 전체 비활성화 및 전력 차단 (0mA 대기전력 모드 / 고임피던스 격리)
void forceAllRelaysOff() {
  // 1채널 메인 릴레이 고임피던스 상태로 전환하여 전류 경로 원천 차단
  pinMode(RELAY_MAIN_PIN, INPUT);
  
  // 2채널 방향 제어 릴레이 꺼짐 상태 고정 (Active-Low 이므로 HIGH)
  digitalWrite(RELAY_DIR_A_PIN, HIGH);
  digitalWrite(RELAY_DIR_B_PIN, HIGH);
  pinMode(RELAY_DIR_A_PIN, OUTPUT);
  pinMode(RELAY_DIR_B_PIN, OUTPUT);
  
  Serial.println("[SAFETY] All relays forced OFF and isolated (0mA standby).");
}

// 릴레이 상태 통합 제어 (합선/쇼트 방지 Mutex 락 설계)
// mainOn: true(메인전원 공급/ON), false(차단/OFF)
// dirOpen: true(정회전/개방), false(차단/NC)
// dirClose: true(역회전/폐쇄), false(차단/NC)
void setRelayStates(bool mainOn, bool dirOpen, bool dirClose) {
  // [쇼트 방지 1차 락] 방향성 릴레이가 동시에 LOW(ON)로 설정되는 것 원천 금지
  if (dirOpen && dirClose) {
    Serial.println("[EMERGENCY] Interlock block! Attempted to turn on both directions simultaneously. Forcing OFF.");
    forceAllRelaysOff();
    currentState = STATE_EMERGENCY_STOP;
    return;
  }

  static bool lastDirOpen = false;
  static bool lastDirClose = false;
  
  bool dirChanged = (dirOpen != lastDirOpen) || (dirClose != lastDirClose);
  
  if (dirChanged) {
    // [쇼트 방지 2차 락] 방향 릴레이 상태 변경 전, 1채널 릴레이를 무조건 OFF로 설정하고 대기
    pinMode(RELAY_MAIN_PIN, INPUT); 
    delay(100); // 기계 접점 이탈을 위한 하드웨어 딜레이 가드(100ms)
    
    // 방향 제어 핀 설정 (Active-Low)
    if (dirOpen) {
      digitalWrite(RELAY_DIR_A_PIN, LOW);
      digitalWrite(RELAY_DIR_B_PIN, HIGH);
    } else if (dirClose) {
      digitalWrite(RELAY_DIR_A_PIN, HIGH);
      digitalWrite(RELAY_DIR_B_PIN, LOW);
    } else {
      digitalWrite(RELAY_DIR_A_PIN, HIGH);
      digitalWrite(RELAY_DIR_B_PIN, HIGH);
    }
    
    delay(100); // 릴레이 기계 접점이 확실히 붙을 때까지 대기(100ms)
    
    lastDirOpen = dirOpen;
    lastDirClose = dirClose;
  }

  // 메인 전원 제어
  if (mainOn) {
    pinMode(RELAY_MAIN_PIN, OUTPUT);
    digitalWrite(RELAY_MAIN_PIN, LOW); // ON (Active-Low)
  } else {
    pinMode(RELAY_MAIN_PIN, INPUT); // OFF (INPUT 모드로 전환하여 고임피던스로 누설전류 리셋)
  }
}

// 초기 부팅 글리치 방지 릴레이 설정 (규칙 1 준수)
void initRelaysSafely() {
  // pinMode 정의 전 HIGH를 먼저 쓰거나 INPUT 모드로 설정해 글리치 차단
  pinMode(RELAY_MAIN_PIN, INPUT);
  
  digitalWrite(RELAY_DIR_A_PIN, HIGH);
  digitalWrite(RELAY_DIR_B_PIN, HIGH);
  pinMode(RELAY_DIR_A_PIN, OUTPUT);
  pinMode(RELAY_DIR_B_PIN, OUTPUT);
  
  Serial.println("[SYSTEM] Relays initialized with high-impedance startup guard.");
}

// ==========================================
// 6. 초음파 센서 구동 및 중간값 필터 (Median Filter)
// ==========================================

long getRawDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // pulseIn 타임아웃을 10000us(약 10ms, 최장거리 약 1.7m)로 타이트하게 줄여 loop() 블로킹 최소화
  long duration = pulseIn(ECHO_PIN, HIGH, 10000);

  if (duration == 0)
    return 999;                  // 측정 실패 또는 제한거리 초과 시 999cm 반환
  return (duration * 0.034) / 2; // cm 단위로 환산
}

void updateDistanceBuffer() {
  long rawDist = getRawDistance();
  distBuffer[filterIdx] = rawDist;
  filterIdx = (filterIdx + 1) % FILTER_SIZE;
}

long getFilteredDistance() {
  long sorted[FILTER_SIZE];
  for (int i = 0; i < FILTER_SIZE; i++) {
    sorted[i] = distBuffer[i];
  }
  
  // 단순 버블 소팅
  for (int i = 0; i < FILTER_SIZE - 1; i++) {
    for (int j = i + 1; j < FILTER_SIZE; j++) {
      if (sorted[i] > sorted[j]) {
        long temp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = temp;
      }
    }
  }
  
  // 5개 중 중간값 반환 (튀는 노이즈 완벽 격리)
  return sorted[FILTER_SIZE / 2];
}

// ==========================================
// 7. INA219 및 센서 모니터링 수집
// ==========================================

void readSensors() {
  if (hasINA219) {
    // Vin+와 Vin- 단자 전위차를 수집하여 12V 배터리 전압 및 소모 전류 측정
    batteryVoltage = ina219.getBusVoltage_V() + (ina219.getShuntVoltage_mV() / 1000.0);
    motorCurrent = ina219.getCurrent_mA();
  } else {
    // INA219 모듈 미연결/고장 시 테스트 및 부팅 지속을 위해 디폴트 안심값 매핑
    batteryVoltage = 12.2; 
    motorCurrent = 0.0;
  }
}

// ==========================================
// 8. 비동기 웹서버 API 핸들러
// ==========================================

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String stateStr;
  switch(currentState) {
    case STATE_IDLE: stateStr = "IDLE"; break;
    case STATE_OPEN_START: stateStr = "OPEN_START"; break;
    case STATE_OPENING: stateStr = "OPENING"; break;
    case STATE_HOLD: stateStr = "HOLD"; break;
    case STATE_CLOSE_START: stateStr = "CLOSE_START"; break;
    case STATE_CLOSING: stateStr = "CLOSING"; break;
    case STATE_EMERGENCY_STOP: stateStr = "EMERGENCY_STOP"; break;
    case STATE_BATTERY_LOW_SHUTDOWN: stateStr = "BATTERY_LOW_SHUTDOWN"; break;
  }
  
  unsigned long elapsed = 0;
  if (currentState == STATE_OPENING || currentState == STATE_CLOSING || currentState == STATE_HOLD) {
    elapsed = millis() - stateTimer;
  }
  
  String json = "{";
  json += "\"state\":\"" + stateStr + "\",";
  json += "\"battery\":" + String(batteryVoltage, 2) + ",";
  json += "\"current\":" + String(motorCurrent, 2) + ",";
  json += "\"distance\":" + String(currentDistance) + ",";
  json += "\"time\":" + String(elapsed);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleRelease() {
  if (currentState == STATE_EMERGENCY_STOP) {
    currentState = STATE_IDLE;
    isCooldown = true;
    cooldownTimer = millis();
    Serial.println("[SYSTEM] Emergency Lock released from web console. Re-entering IDLE.");
    server.send(200, "application/json", "{\"status\":\"released\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not in emergency state\"}");
  }
}

// ==========================================
// 9. Setup & Loop
// ==========================================

void setup() {
  Serial.begin(115200);
  
  // 1. 릴레이 초기화 (부팅 시 오동작/글리치 완전 방어)
  initRelaysSafely();
  
  // 2. 초음파 센서 핀 설정
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  
  // 3. I2C 및 INA219 초기화
  Wire.begin(I2C_SDA, I2C_SCL);
  if (ina219.begin()) {
    hasINA219 = true;
    Serial.println("[SENSOR] INA219 Current sensor found and initialized.");
  } else {
    Serial.println("[WARNING] INA219 not found on I2C bus! Using dummy simulation values.");
  }
  
  // 4. Wi-Fi AP 및 Web Server 시작
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("[WIFI] AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("[WIFI] AP IP Address: ");
  Serial.println(IP);
  
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/release", handleRelease);
  server.begin();
  Serial.println("[WIFI] Web Dashboard Server active on port 80.");
  
  // 초음파 버퍼 쓰레기값 채우기
  for (int i = 0; i < FILTER_SIZE; i++) {
    updateDistanceBuffer();
  }
  
  Serial.println("[SYSTEM] System initialization complete. IDLE status active.\n");
}

void loop() {
  // 웹 요청 비동기 서비스 처리
  server.handleClient();
  
  // 50ms 마다 초음파 읽기 및 필터링
  if (millis() - sensorTimer >= 50) {
    sensorTimer = millis();
    updateDistanceBuffer();
    currentDistance = getFilteredDistance();
  }
  
  // 200ms 마다 INA219 센서 전압/전류 갱신
  static unsigned long lastReading = 0;
  if (millis() - lastReading >= 200) {
    lastReading = millis();
    readSensors();
  }
  
  // 쿨다운 해제 관리
  if (isCooldown && (millis() - cooldownTimer >= COOLDOWN_TIME)) {
    isCooldown = false;
    Serial.println("[SYSTEM] Cooldown active state finished.");
  }
  
  // 배터리 저전압 3초 필터 보호 가드
  static unsigned long lowVoltGuardStart = 0;
  if (batteryVoltage < VOLTAGE_SHUTDOWN_LIMIT && currentState != STATE_BATTERY_LOW_SHUTDOWN) {
    if (lowVoltGuardStart == 0) {
      lowVoltGuardStart = millis();
    } else if (millis() - lowVoltGuardStart >= 3000) {
      // 3초 지속적 저전압 발생 시 셧다운 강제 시동
      Serial.printf("[BATTERY] Critical battery voltage: %.2fV! Launching Shutdown.\n", batteryVoltage);
      currentState = STATE_BATTERY_LOW_SHUTDOWN;
      stateTimer = millis();
    }
  } else {
    lowVoltGuardStart = 0;
  }
  
  // ==========================================
  // 논블로킹 유한 상태 머신 (Finite State Machine)
  // ==========================================
  switch(currentState) {
    
    case STATE_IDLE:
      setRelayStates(false, false, false); // 모든 전력 차단
      
      // 사람이 감지 범위 이내로 들어오고, 쿨다운이 끝난 상태인 경우
      if (currentDistance > 0 && currentDistance < DIST_THRESHOLD && !isCooldown) {
        Serial.printf("[SENSOR] Human approach detected: %ld cm. Starting opening.\n", currentDistance);
        currentState = STATE_OPEN_START;
      }
      break;
      
    case STATE_OPEN_START:
      Serial.println("[STATE] Opening Started. Toggling H-Bridge.");
      // 1채널 ON, 방향 정회전(A=ON, B=OFF)
      setRelayStates(true, true, false); 
      stateTimer = millis();
      currentState = STATE_OPENING;
      break;
      
    case STATE_OPENING:
      // 모터 기동 전류(Inrush) 회피를 위해 기동 후 300ms 이후부터 과전류 감지 활성화
      if (millis() - stateTimer > 300) {
        if (motorCurrent > CURRENT_STALL_LIMIT) {
          Serial.printf("[EMERGENCY] Stall current detected: %.1fmA. Lock active.\n", motorCurrent);
          forceAllRelaysOff();
          currentState = STATE_EMERGENCY_STOP;
          break;
        }
      }
      
      // 3.8초 구동 타이머 만료 시 개방 홀드 진입
      if (millis() - stateTimer >= ACTUATOR_TIME) {
        Serial.println("[STATE] Opening completed. Entering hold.");
        setRelayStates(false, false, false); // 대기시간 동안 전력 완전 차단 (절전)
        stateTimer = millis();
        currentState = STATE_HOLD;
      }
      break;
      
    case STATE_HOLD:
      setRelayStates(false, false, false); // 대기 전력 완전 절약
      
      // 사람이 대기 범위 바깥으로 빠져나가면 즉시 조기 종료 시퀀스 돌입
      if (currentDistance > DIST_THRESHOLD) {
        Serial.printf("[SENSOR] Human departed: %ld cm. Closing early.\n", currentDistance);
        currentState = STATE_CLOSE_START;
      }
      // 또는 5초가 경과한 경우
      else if (millis() - stateTimer >= WAIT_TIME) {
        Serial.println("[STATE] Hold timeout reached. Starting closing.");
        currentState = STATE_CLOSE_START;
      }
      break;
      
    case STATE_CLOSE_START:
      Serial.println("[STATE] Closing Started. Toggling H-Bridge.");
      // 1채널 ON, 방향 역회전(A=OFF, B=ON)
      setRelayStates(true, false, true);
      stateTimer = millis();
      currentState = STATE_CLOSING;
      break;
      
    case STATE_CLOSING:
      // 1) 닫히는 도중 안전 가드: 사람이 다시 접근하면 즉시 정지 후 다시 개방 시퀀스 전이
      if (currentDistance > 0 && currentDistance < DIST_THRESHOLD) {
        Serial.printf("[SAFETY] Human re-approach during closing: %ld cm! Reopening.\n", currentDistance);
        // 메인 차단 및 역방향 해제
        setRelayStates(false, false, false);
        currentState = STATE_OPEN_START;
        break;
      }
      
      // 2) 닫히는 도중 과전류 가드: Stall 혹은 이물질 협착 방지
      if (millis() - stateTimer > 300) {
        if (motorCurrent > CURRENT_STALL_LIMIT) {
          Serial.printf("[EMERGENCY] Stall current during closing: %.1fmA. Lock active.\n", motorCurrent);
          forceAllRelaysOff();
          currentState = STATE_EMERGENCY_STOP;
          break;
        }
      }
      
      // 3.8초 구동 타이머 만료 시 닫힘 종료
      if (millis() - stateTimer >= ACTUATOR_TIME) {
        Serial.println("[STATE] Closing completed. Entering standby.");
        setRelayStates(false, false, false); // 모든 전력 차단
        isCooldown = true;
        cooldownTimer = millis();
        currentState = STATE_IDLE;
      }
      break;
      
    case STATE_EMERGENCY_STOP:
      // 안전을 위해 릴레이를 영구적으로 끊고 대기
      forceAllRelaysOff();
      
      // 10초마다 시리얼 경고 로그
      static unsigned long emergencyLog = 0;
      if (millis() - emergencyLog >= 10000) {
        emergencyLog = millis();
        Serial.println("[EMERGENCY] Safety Lock is active. Waiting for web console release command...");
      }
      break;
      
    case STATE_BATTERY_LOW_SHUTDOWN:
      Serial.println("[BATTERY] Forced recovery operation active: Opening lid permanently.");
      
      // 마지막 남은 전력을 짜내어 뚜껑을 완전히 열어둠 (관리자 수리 접근성 제공)
      setRelayStates(true, true, false); 
      delay(ACTUATOR_TIME);
      
      // 도달 완료 후 릴레이 보드의 대기전력 마저 0mA로 만들기 위해 모든 핀 고임피던스로 격리
      forceAllRelaysOff();
      
      Serial.println("[BATTERY] Shutdown complete. Board entering deep hibernation. Goodbye.");
      
      // 무한 루프로 동작 동결 (추가 방전 최소화)
      while (true) {
        delay(1000);
      }
      break;
  }
}