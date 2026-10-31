#include "Esp32Hardware.h"
#include <Arduino.h>
#include <Wire.h>

Esp32Hardware::Esp32Hardware(uint8_t trig, uint8_t echo) 
    : trigPin(trig), echoPin(echo) {}

void Esp32Hardware::init() {
    // Initialize I2C with SDA=GPIO22 and SCL=GPIO23 as specified in development manual
    Wire.begin(22, 23);
    Wire.setTimeOut(20);
    if (ina219.begin()) {
        hasINA219 = true;
        Serial.println("[SENSOR] INA219 Current sensor found and initialized.");
    } else {
        Serial.println("[WARNING] INA219 not found on I2C bus! Power measurements unavailable; actuator safety remains inhibited.");
    }
}

void Esp32Hardware::setPinMode(uint8_t pin, uint8_t mode) {
    pinMode(pin, mode);
}

void Esp32Hardware::writePin(uint8_t pin, uint8_t val) {
    digitalWrite(pin, val);
}

int Esp32Hardware::readPin(uint8_t pin) {
    return digitalRead(pin);
}

unsigned long Esp32Hardware::getMillis() {
    return millis();
}

void Esp32Hardware::delayMs(unsigned long ms) {
    delay(ms);
}

PowerReading Esp32Hardware::readPower() {
    if(!hasINA219) {
        if(millis()-lastPowerProbe<5000) return {NAN,NAN,false};
        lastPowerProbe=millis();
        hasINA219=ina219.begin();
        if(!hasINA219) return {NAN,NAN,false};
    }
    const float bus=ina219.getBusVoltage_V(); const bool busOk=ina219.success();
    const float shunt=ina219.getShuntVoltage_mV(); const bool shuntOk=ina219.success();
    const float current=ina219.getCurrent_mA(); const bool currentOk=ina219.success();
    const float voltage=bus+shunt/1000.0f;
    const bool ok=busOk && shuntOk && currentOk && std::isfinite(voltage) &&
                  voltage>0 && voltage<=26 && std::isfinite(current);
    if(!ok) {hasINA219=false; lastPowerProbe=millis(); return {NAN,NAN,false};}
    return {voltage,current,true};
}
float Esp32Hardware::getBatteryVoltage() { return readPower().voltage; }
float Esp32Hardware::getMotorCurrent() { return readPower().currentMa; }

float Esp32Hardware::getDistanceCm() {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    // Increase timeout to 30000us (~5.1m max distance) to prevent timing misses from network interrupts
    long duration = pulseIn(echoPin, HIGH, 30000);

    if (duration == 0) {
        return 999.0f; // Return out-of-range flag if timeout or error
    }
    return (duration * 0.034f) / 2.0f;
}
