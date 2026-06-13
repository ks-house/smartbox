#ifndef HARDWARE_INTERFACE_H
#define HARDWARE_INTERFACE_H

#include <stdint.h>

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
};

#endif // HARDWARE_INTERFACE_H
