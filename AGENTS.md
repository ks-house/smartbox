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

---

## 5. Future Expansion Context
When the user asks to add new features, keep these future goals in mind:
* **IoT Integration:** The system is planned to integrate with SmartThings via Matter protocol or Home Assistant via MQTT. Keep the main loop clean to allow for network polling.
* **Telemetry:** Future updates may require sending usage data to Splunk or an AI Agent skillset hub (CODE-Skills). Design data-gathering functions modularly.
* **Power Saving:** ESP32 Light Sleep / Deep Sleep implementations may be required later. Ensure GPIO wake-up logic is considered if refactoring the sensor reading loop.