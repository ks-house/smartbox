# 🤖 System Prompt for AI Coding Assistants (AGENTS.md)

This document provides absolute constraints, hardware context, and architectural guidelines for any AI coding assistant (e.g., Cline, Cursor, GitHub Copilot) modifying or extending the **SmartBox** repository. 

**Read this entire document before generating or modifying any code.**

---

## 1. Project Context & Philosophy
* **Project Name:** SmartBox (Smart Trash Can Controller)
* **Goal:** A contactless, automated trash can lid opener using an ultrasonic sensor and a 12V linear actuator.
* **Core Philosophy:** **"Hardware Safety First."** This project deals with 12V high-current loads and mechanical relays. A software bug can cause a physical short circuit, fire, or hardware damage. 

---

## 2. Hardware Architecture & Pin Mapping
You must adhere strictly to these GPIO assignments. Do not arbitrarily change pins.

* **MCU:** ESP32-C6 (RISC-V, 16MB Flash, WiFi 6 / BLE 5)
* **Framework:** Arduino (via custom `pioarduino` core on PlatformIO)
* **Pins:**
  * `GPIO 4`: Ultrasonic Sensor (AJ-SR04T) - TRIG
  * `GPIO 5`: Ultrasonic Sensor (AJ-SR04T) - ECHO
  * `GPIO 6`: 1-Channel Relay (MAIN Power Cutoff - 12V)
  * `GPIO 7`: 2-Channel Relay (IN1 - Actuator Forward / Open)
  * `GPIO 8`: 2-Channel Relay (IN2 - Actuator Backward / Close)

---

## 3. 🚨 CRITICAL SAFETY RULES (NEVER VIOLATE) 🚨
The motor direction is controlled by a 2-channel relay. The main 12V power is controlled by a 1-channel relay. **You must strictly follow this interlocking sequence.**

1. **NO SIMULTANEOUS ACTIVATION:** You MUST NEVER set `GPIO 7` (IN1) and `GPIO 8` (IN2) to `HIGH` at the same time. This will short-circuit the 12V power supply.
2. **CUT POWER BEFORE SWITCHING:** Before changing the state of the 2-channel relays (Direction), you MUST set the 1-channel relay (`GPIO 6`) to `LOW` to cut off the 12V main power. 
   * *Sequence:* MAIN OFF -> Wait 100ms -> Change DIR -> Wait 100ms -> MAIN ON.
3. **DEFAULT STATE IS LOW:** In `setup()` and during idle/sleep states, all relays (`GPIO 6, 7, 8`) MUST be explicitly written to `LOW`.
4. **ACTUATOR TIMEOUT:** The actuator stroke takes ~3.5 seconds. Always implement a timeout (e.g., 3800ms) to stop the relays even if the actuator's internal limit switch is expected to work.

---

## 4. Software Engineering Standards

### 4.1. Non-Blocking Architecture (State Machine)
* While V1 may use `delay()`, any code upgrades involving Wi-Fi, BLE, or Matter integrations **MUST use non-blocking `millis()` timers** and a State Machine pattern. 
* Do not use `delay()` in the main loop if network capabilities are active, as it will trigger the ESP32 Watchdog Timer (WDT) or drop connections.

### 4.2. Libraries and Dependencies
* Rely on the standard Arduino core (`<Arduino.h>`) as much as possible.
* Do not introduce third-party libraries for simple GPIO manipulation, ultrasonic reading, or basic timing. 
* If Wi-Fi/MQTT/Matter is requested, use the official Espressif libraries compatible with the Arduino core.

### 4.3. Serial Debugging
* Maintain `Serial.begin(115200)` in `setup()`.
* All state transitions MUST be logged to the Serial monitor (e.g., `Serial.println("[STATE] Opening Lid");`, `Serial.printf("[SENSOR] Dist: %d cm\n", dist);`). This is vital for the human developer to debug hardware without a logic analyzer.

### 4.4. Memory and Partitions
* The board has 16MB of Flash. If adding OTA (Over-The-Air) updates or a Web Server, ensure you reference the `default_16MB.csv` partition scheme in instructions.

### 4.5. Documentation and Link Standards
* **Relative Links Only:** When linking files within repository markdown documents (e.g., `README.md`, reports, manual), **always use relative paths** (e.g., `[main.cpp](../src/main.cpp)` or `[BOM](HARDWARE_BOM.md)`).
* **No Absolute Local Paths:** **Never use absolute local paths** (e.g., `file:///c:/...` or `C:\Users\...`) in committed files. This prevents path breaking on other developers' machines.
### 4.6. 전체 프롬프트 저장 규칙 (Export Transcript Rule)
* 사용자가 **"전체 프롬프트 저장 해줘"** 또는 이에 상응하는 대화록 저장 요청을 하는 경우, AI 에이전트는 다음 자동화 스크립트를 실행하여 현재까지의 대화 로그를 Markdown 문서로 내보내야 합니다.
  * *실행 명령어:* `python scripts/export_docs.py [주제명] --num [순번]` (예: `개발및검증_최종` / `03`)
  * 내보낸 결과 파일(`prompts/YYYYMMDD[순번]_[주제명]_prompt.md`)과 복사/생성된 보고서 템플릿(`reports/YYYYMMDD[순번]_[주제명]_report.md`)을 확인하고 Git에 즉각 스테이징 및 커밋하여 변경 이력을 영구 보존해야 합니다.

### 4.7. 보안 및 민감 정보 보호 규정 (Secrets & Security Regulations)
* WiFi 비밀번호, API 토큰, 개인 NAS 접속 URL/포트 번호 등 보안 민감 정보(Secrets)를 소스 코드(`.cpp`, `.h`), 설정 파일(`.ini`), 스크립트, 혹은 마크다운 문서에 **절대 하드코딩하여 노출하지 마십시오.**
* 모든 민감 정보는 `include/secrets.h`에 매크로(Constant)로 정의하여 사용해야 하며, 해당 파일은 Git에 트래킹되지 않도록 항상 `.gitignore`로 제외 관리합니다.
* 외부 기고 및 템플릿 제공을 위해 더미 값이 적힌 `include/secrets.h.example`을 제공하고 유지 관리해야 합니다.
* CI/CD 파이프라인(GitHub Actions) 환경에서는 빌드 직전에 GitHub Secrets 주입 방식으로 헤더 파일(`secrets.h`)을 빌드 러너 환경 내에서 동적 생성하도록 가이드하고 구현해야 합니다.

---

## 5. Future Expansion Context
When the user asks to add new features, keep these future goals in mind:
* **IoT Integration:** The system is planned to integrate with SmartThings via Matter protocol or Home Assistant via MQTT. Keep the main loop clean to allow for network polling.
* **Telemetry:** Future updates may require sending usage data to Splunk or an AI Agent skillset hub (CODE-Skills). Design data-gathering functions modularly.
* **Power Saving:** ESP32 Light Sleep / Deep Sleep implementations may be required later. Ensure GPIO wake-up logic is considered if refactoring the sensor reading loop.