# 📋 26062013_무인자동OTA_report.md

- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 20일
- **주제:** 무인자동OTA 시스템 구현 및 연동

---

## 1. 이슈 개요 (Issue Overview)
태양광 스마트 자동 수거함의 완전 무인화(Zero-Touch) 운영을 위해 기존 웹 대시보드 기반의 수동 OTA 방식에서 시스템이 직접 새벽 시간대에 펌웨어 버전을 점검하고 자동으로 업데이트를 수행하는 **무인 자동 OTA (Auto-OTA) 시스템**의 도입이 요구되었습니다. 

## 2. 설계 및 제약 조건 (Design & Constraints Analysis)
1. **NTP 시간 기준 확보:** 외부 무선 인터넷(STA 모드) 연결이 완료되면 즉시 한국 표준시(KST, UTC+9)로 내장 RTC 시간을 pool.ntp.org 및 nist.gov NTP 서버로부터 동기화하여 기준 시간대를 확립해야 합니다.
2. **논블로킹 스케줄링:** 메인 루프 내부의 상태 머신(FSM) 센서 측정 루프나 비동기 웹 대시보드 서버가 다운되지 않도록 30초 단위의 비블로킹 폴링 기법으로 시간을 감시하며, 매일 새벽 3시 KST에 단 1회 업데이트 태스크를 구동합니다.
3. **🚨 Pre-OTA 하드웨어 인터락 (안전장치):** 플래시에 펌웨어 바이너리를 쓰는 전처리 과정에서 노이즈나 부팅 핀 전위 변화로 모터 방향 제어용 2채널 릴레이가 동시 통전되는 H-브릿지 12V 쇼트 사고를 하드웨어 수준에서 완벽 방지해야 합니다. 따라서 플래싱 시작 전 `controller.forceAllRelaysOff()`를 호출해 모든 릴레이(GPIO 6, 7, 8)를 `INPUT`(고임피던스) 상태로 격리하고 FSM을 `STATE_OTA_UPDATING`으로 고정합니다.
4. **네트워크 예외 및 복구:** 다운로드 중 Wi-Fi 끊김, 연결 시간 초과, SSL 핸드셰이크 실패 등의 예외 발생 시 시스템 정지를 방지하기 위해 예외 처리를 거쳐 정상 FSM 상태인 `STATE_IDLE`로 자동 복구해야 합니다.

## 3. 해결 설계 및 구현 (Solution Design & Implementation)

### A. NTP 시간 동기화 ([src/WifiManager.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/WifiManager.cpp))
IP 주소를 획득하는 Wi-Fi `GOT_IP` 이벤트 시점에서 timezone 설정을 수행하여 RTC 시간 동기화를 자동 시작하도록 결합했습니다.
```cpp
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("[WIFI] Station got IP address: ");
            Serial.println(WiFi.localIP());
            connected = true;
            configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
            Serial.println("[TIME] NTP Sync initialized (KST, UTC+9).");
            break;
```

### B. AutoOtaManager 신설 및 스케줄러 구현 ([src/AutoOtaManager.cpp](file:///c:/Users/shcat/Documents/PlatformIO/Projects/smartbox/src/AutoOtaManager.cpp))
30초마다 RTC 시간을 논블로킹으로 대조하여 새벽 3시(hour == 3)에 도달하면 `xTaskCreate`를 통해 16KB 스택의 독립 백그라운드 태스크를 실행합니다.
```cpp
void AutoOtaManager::update() {
    if (!WifiManager::isConnected()) return;
    
    unsigned long now = millis();
    if (now - lastScheduleCheck < 30000) return;
    lastScheduleCheck = now;
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) return;
    
    if (timeinfo.tm_hour == 3) {
        if (lastOtaCheckDay != timeinfo.tm_mday) {
            lastOtaCheckDay = timeinfo.tm_mday;
            Serial.println("[AUTO-OTA] Scheduled trigger time (3:00 AM KST) reached. Launching AutoOtaTask...");
            startOtaUpdate(false);
        }
    }
}
```

### C. 비동기 버전 점검 및 하드웨어 안전 업데이트 태스크
HTTPS GET 요청을 Synology NAS 배포 서버(`version.json`)로 전송하여 응답을 받고, 메모리 최적화를 위해 경량 String 파서로 `"version"` 필드를 추출합니다. 빌드 버전이 다른 경우 즉시 릴레이를 완전 오프하고 `STATE_OTA_UPDATING` 진입 후 다운로드를 실행합니다.
```cpp
void AutoOtaManager::otaTaskFunction(void *pvParameters) {
    bool force = (bool)pvParameters;
    bool needsUpdate = false;
    
    if (!force) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, VERSION_URL);
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            String remoteVersion = parseJsonField(payload, "version");
            if (!remoteVersion.isEmpty() && remoteVersion != controllerPtr->getFirmwareVersion()) {
                needsUpdate = true;
            }
        }
        http.end();
    } else {
        needsUpdate = true;
    }
    
    if (needsUpdate) {
        // 안전 결선 차단 인터락
        controllerPtr->forceAllRelaysOff();
        controllerPtr->transitionTo(STATE_OTA_UPDATING);
        delay(500); // 릴레이 물리 점접 완전 이격 대기
        
        WiFiClientSecure client;
        client.setInsecure();
        t_httpUpdate_return ret = httpUpdate.update(client, FIRMWARE_URL);
        
        if (ret == HTTP_UPDATE_FAILED || ret == HTTP_UPDATE_NO_UPDATES) {
            controllerPtr->transitionTo(STATE_IDLE); // 실패 복구
        } else if (ret == HTTP_UPDATE_OK) {
            delay(1500);
            ESP.restart();
        }
    }
    vTaskDelete(NULL);
}
```

## 4. 검증 결과 (Verification Results)
* **타겟 환경 컴파일 검증:** `pio run -e esp32-c6-devkitc-1` 명령을 통해 컴파일 에러 없이 빌드가 성공적으로 완료되었습니다.
  - **RAM 사용량:** 14.0% (45,744 bytes / 327,680 bytes)
  - **Flash 사용량:** 19.4% (1,274,374 bytes / 6,553,600 bytes)
* **릴레이 격리 검증:** 비동기 OTA 진입 즉시 모든 릴레이 포트가 `INPUT`(0mA 대기 상태) 모드로 무조건 격리되어 모터 오작동 및 전원 단락을 완전히 원천 봉쇄함을 코드 정적 분석으로 더블 체크했습니다.
* **복구 안정성:** 펌웨어 업데이트 실패 시, FSM이 동결 상태에 머무르지 않고 즉각 `STATE_IDLE`로 되돌아와 수거함 센서 폴링 동작을 재개함을 구조적으로 보증합니다.
