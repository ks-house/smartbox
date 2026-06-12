# 🛠️ SmartBox Hardware BOM & Specifications (HARDWARE_BOM.md)

This document catalogs the EXACT hardware components purchased and used in the SmartBox project. AI Agents must reference this list to ensure proper voltage levels, signal logic, and timing requirements.

## 1. Controller & Sensors (Control Layer)

### 1.1. MCU: ESP32-C6-DevKitC-1
* **Specs:** RISC-V Single-Core, 16MB Flash, Wi-Fi 6 / BLE 5 / Matter compatible.
* **Logic Voltage:** 3.3V TTL (GPIO pins are NOT 5V tolerant).
* **Development Core:** `pioarduino` custom core (PlatformIO environment).

### 1.2. Ultrasonic Sensor: AJ-SR04T
* **Specs:** Integrated Waterproof Ultrasonic Distance Sensor (Separate Probe).
* **Operating Voltage:** DC 5V (Powered via ESP32 5V/VBUS pin).
* **Pins Used:** `GPIO 4` (Trig), `GPIO 5` (Echo).

### 1.3. Current/Power Sensor: INA219 (MCU-219)
* **Specs:** I2C Zero-Drift Bi-directional Current/Power Monitoring Monitor.
* **Role:** Monitors current draw from the 12V battery and actuator to check power efficiency and detect motor stalling.
* **Interface:** I2C (`SDA` / `SCL` pins on ESP32-C6).

---

## 2. Power & Actuation (Power Layer)

### 2.1. Linear Actuator (선형 액추에이터) x 2
* **Specs:** **DC 12V / 100N Force / 200mm Stroke Length**.
* **Quantity:** 2 units.
* **Travel Speed:** ~60mm/s. Full stroke operation takes ~3.5 to 3.8 seconds.
* **Software Timeout:** Set to **3800ms** for full extension/retraction safety.

### 2.2. 1-Channel Relay Module (Main Power Cutoff)
* **Trigger Type:** **Low-Level Trigger (L-Active)**.
* **Role:** Master safety cutoff for the 12V load line. Controlled dynamically via INPUT/OUTPUT mode toggles to prevent startup glitches.
* **Pin Used:** `GPIO 6`.

### 2.3. 2-Channel Relay Module (Direction Control)
* **Trigger Type:** **Low-Level Trigger (L-Active)**.
* **Configuration:** Wired as an H-Bridge to reverse polarity for the 12V actuators.
* **Safety Rule:** Never pull both channels LOW simultaneously. Always toggle after Master Relay is OFF.
* **Pins Used:** `GPIO 7` (Forward/Open), `GPIO 8` (Backward/Close).

### 2.4. DC-DC Buck Converter: LM2596
* **Specs:** Step-down voltage regulator module.
* **Role:** Steps down the stable 12V output from the solar controller to 5V to safely power the ESP32-C6 and sensors.

### 2.5. Manual Override: Waterproof Toggle Switch
* **Role:** Physical master power switch or manual override trigger attached to the enclosure for maintenance.

---

## 3. Solar Energy Infrastructure (Energy Layer)

### 3.1. Solar Charge Controller
* **Specs:** **10A** Solar Charge Regulator.
* **Role:** Manages power routing from the Solar Panel to the Battery, and provides a stable 12V load line to the 1-Channel Relay.

### 3.2. Battery
* **Specs:** **DC 12V / 7Ah Lead-Acid Battery (납축전지)**.
* **Role:** Main energy storage for the entire system, primarily driving the 12V 100N actuators.

### 3.3. Solar Panel
* **Role:** Captures solar energy to charge the 12V 7Ah battery via the 10A controller.

---

## 4. Enclosure & Environment (Physical Layer)

### 4.1. Waterproof High-Box (방수 하이박스)
* **Role:** Main weatherproof enclosure protecting the ESP32-C6, relays, buck converter, and wiring from external elements.

### 4.2. Ventilation Cover (환기구 커버)
* **Role:** Provides airflow within the high-box to prevent heat buildup from the LM2596 and relays while maintaining splash resistance.
