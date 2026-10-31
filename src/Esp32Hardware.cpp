#include "Esp32Hardware.h"
#include <Arduino.h>
#include <Wire.h>

Esp32Hardware::Esp32Hardware(uint8_t trig, uint8_t echo) 
    : trigPin(trig), echoPin(echo) {}

void Esp32Hardware::init() {
    // Initialize I2C with SDA=GPIO22 and SCL=GPIO23 as specified in development manual
    Wire.begin(22, 23);
    Wire.setTimeOut(20);
    const auto first=readPower();
    Serial.println(first.valid ? "[SENSOR] INA219 triggered profile verified." :
        "[WARNING] Power measurement unavailable; actuator protection remains active.");
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

namespace {
class WireInaBus:public Ina219Bus {
 public:
  bool writeRegister(uint8_t reg,uint16_t value) override {
    Wire.beginTransmission(uint8_t(0x40));
    const bool accepted=Wire.write(reg)==1 && Wire.write(uint8_t(value>>8))==1 && Wire.write(uint8_t(value))==1;
    const uint8_t status=Wire.endTransmission();
    return accepted && status==0;
  }
  bool readRegister(uint8_t reg,uint16_t &value) override {
    Wire.beginTransmission(uint8_t(0x40));
    const bool accepted=Wire.write(reg)==1;
    const uint8_t status=Wire.endTransmission(false);
    if(!accepted || status!=0 || Wire.requestFrom(uint8_t(0x40),size_t(2),true)!=2) return false;
    const int high=Wire.read(),low=Wire.read();
    if(high<0 || low<0) return false;
    value=uint16_t((uint16_t(high)<<8)|uint16_t(low));return true;
  }
  uint32_t nowMs() override {return static_cast<uint32_t>(millis());}
  void waitMs(uint32_t ms) override {delay(ms);}
};
}
PowerReading Esp32Hardware::readPower() {
    WireInaBus bus;return sampleIna219(bus);
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
