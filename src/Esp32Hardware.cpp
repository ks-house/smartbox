#include "Esp32Hardware.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BusIO_Register.h>

Esp32Hardware::Esp32Hardware(uint8_t trig, uint8_t echo) 
    : trigPin(trig), echoPin(echo) {}

void Esp32Hardware::init() {
    // Initialize I2C with SDA=GPIO22 and SCL=GPIO23 as specified in development manual
    initializePowerReader();
}
bool Esp32Hardware::initializePowerReader() {
    lastPowerProbe=millis();
    const bool busReady=Wire.begin(22,23);
    Wire.setTimeOut(50); // Restore the pre-regression Arduino timeout.
    powerReady=busReady && ina219.begin(&Wire) && registers.begin(false);
    if(powerReady)delayMicroseconds(2000); // Complete the first12-bit shunt+bus conversion before validation.
    Serial.println(powerReady ? "[SENSOR] INA219 continuous baseline initialized." :
        "[WARNING] INA219 init failed; outputs remain protected; retry in5s.");
    if(!powerReady)lastPowerError={NAN,NAN,false,PowerReadFault::NOT_INITIALIZED,1};
    return powerReady;
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
class AdafruitContinuousSource:public Ina219ContinuousSource {
 public:
  AdafruitContinuousSource(Adafruit_INA219& sensor,Adafruit_I2CDevice& device):sensor_(sensor),device_(device){}
  bool readConfiguration(uint16_t &v)override {Adafruit_BusIO_Register reg(&device_,0,2,MSBFIRST);return reg.read(&v);}
  bool readCalibration(uint16_t &v)override {Adafruit_BusIO_Register reg(&device_,5,2,MSBFIRST);return reg.read(&v);}
  bool readBusVoltage(float &v)override {v=sensor_.getBusVoltage_V();return sensor_.success();}
  bool readShuntVoltageMv(float &v)override {v=sensor_.getShuntVoltage_mV();return sensor_.success();}
  bool readCurrentMa(float &v)override {v=sensor_.getCurrent_mA();return sensor_.success();}
 private:
  Adafruit_INA219& sensor_;Adafruit_I2CDevice& device_;
};
}
PowerReading Esp32Hardware::readPower() {
    if(!powerReady && uint32_t(millis()-lastPowerProbe)>=5000)initializePowerReader();
    if(!powerReady)return lastPowerError;
    AdafruitContinuousSource source(ina219,registers);auto reading=sampleIna219Continuous(source);
    if(!reading.valid){
        lastPowerError=reading;
        if(reading.fault!=PowerReadFault::NONFINITE && reading.fault!=PowerReadFault::RANGE){powerReady=false;lastPowerProbe=millis();}
    }
    return reading;
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
