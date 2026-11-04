#ifndef HARDWARE_INTERFACE_H
#define HARDWARE_INTERFACE_H

#include <stdint.h>
#include <cmath>
enum class PowerReadFault : uint8_t {
    NONE=0, CONFIGURATION_IO=1, PROFILE_MISMATCH=2, CALIBRATION_IO=3,
    CALIBRATION_MISMATCH=4, BUS_IO=5, SHUNT_IO=6, CURRENT_IO=7,
    NONFINITE=8, RANGE=9, NOT_INITIALIZED=10
};
struct PowerReading {
    float voltage; float currentMa; bool valid;
    PowerReadFault fault=PowerReadFault::NONE;
    uint8_t driver=0;
    uint16_t configuration=0,calibration=0;
};

#ifdef NATIVE_BUILD
#ifndef INPUT
#define INPUT 0x0
#endif
#ifndef OUTPUT
#define OUTPUT 0x1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x2
#endif
#ifndef HIGH
#define HIGH 0x1
#endif
#ifndef LOW
#define LOW 0x0
#endif
#endif


class HardwareInterface {
public:
    virtual ~HardwareInterface() {}
    
    // GPIO Controls
    virtual void setPinMode(uint8_t pin, uint8_t mode) = 0;
    virtual void writePin(uint8_t pin, uint8_t val) = 0;
    virtual int readPin(uint8_t pin) = 0;
    
    // Time tracking
    virtual unsigned long getMillis() = 0;
    virtual void delayMs(unsigned long ms) = 0;
    
    // Sensors
    virtual float getBatteryVoltage() = 0;
    virtual float getMotorCurrent() = 0;
    virtual float getDistanceCm() = 0;
    virtual PowerReading readPower() {
        const float v=getBatteryVoltage(), c=getMotorCurrent();
        return {v,c,std::isfinite(v) && std::isfinite(c)};
    }
};

#endif // HARDWARE_INTERFACE_H
