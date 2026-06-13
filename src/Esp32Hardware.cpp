#include "Esp32Hardware.h"
#include <Arduino.h>
#include <Wire.h>

Esp32Hardware::Esp32Hardware(uint8_t trig, uint8_t echo) 
    : trigPin(trig), echoPin(echo) {}

void Esp32Hardware::init() {
    // Initialize I2C with SDA=GPIO22 and SCL=GPIO23 as specified in development manual
    Wire.begin(22, 23);
    if (ina219.begin()) {
        hasINA219 = true;
        Serial.println("[SENSOR] INA219 Current sensor found and initialized.");
    } else {
        Serial.println("[WARNING] INA219 not found on I2C bus! Using dummy simulation values.");
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

float Esp32Hardware::getBatteryVoltage() {
    if (hasINA219) {
        return ina219.getBusVoltage_V() + (ina219.getShuntVoltage_mV() / 1000.0f);
    }
    return 12.2f; // Safely return nominal battery voltage if hardware is missing
}

float Esp32Hardware::getMotorCurrent() {
    if (hasINA219) {
        return ina219.getCurrent_mA();
    }
    return 0.0f; // Return 0mA if hardware is missing
}

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
