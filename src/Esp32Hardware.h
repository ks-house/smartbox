#ifndef ESP32_HARDWARE_H
#define ESP32_HARDWARE_H

#include "HardwareInterface.h"
#include "Ina219Sampler.h"
#include <Adafruit_INA219.h>
#include <Adafruit_I2CDevice.h>

class Esp32Hardware : public HardwareInterface {
private:
    Adafruit_INA219 ina219;
    Adafruit_I2CDevice registers{0x40, &Wire};
    bool powerReady=false;
    PowerReading lastPowerError{NAN,NAN,false,PowerReadFault::NOT_INITIALIZED,1};
    unsigned long lastPowerProbe=0;
    bool initializePowerReader();
    uint8_t trigPin;
    uint8_t echoPin;

public:
    Esp32Hardware(uint8_t trig, uint8_t echo);
    virtual ~Esp32Hardware() {}

    void init();

    // HardwareInterface implementation
    void setPinMode(uint8_t pin, uint8_t mode) override;
    void writePin(uint8_t pin, uint8_t val) override;
    int readPin(uint8_t pin) override;
    
    unsigned long getMillis() override;
    void delayMs(unsigned long ms) override;
    
    float getBatteryVoltage() override;
    float getMotorCurrent() override;
    float getDistanceCm() override;
    PowerReading readPower() override;
};

#endif // ESP32_HARDWARE_H
