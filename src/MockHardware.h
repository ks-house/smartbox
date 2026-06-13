#ifndef MOCK_HARDWARE_H
#define MOCK_HARDWARE_H

#include "HardwareInterface.h"

#ifndef NATIVE_BUILD
#include <Arduino.h>
#endif

#include <map>
#include <vector>

enum EventType {
    EVENT_WRITE,
    EVENT_MODE
};

struct PinEvent {
    EventType type;
    uint8_t pin;
    uint8_t value; // For EVENT_WRITE: HIGH/LOW, For EVENT_MODE: INPUT/OUTPUT
    unsigned long timestamp;
};

class MockHardware : public HardwareInterface {
private:
    unsigned long mockMillis = 0;
    std::map<uint8_t, uint8_t> pinModes;
    std::map<uint8_t, uint8_t> pinValues;
    std::vector<PinEvent> eventHistory;
    float mockBatteryVoltage = 12.0f;
    float mockMotorCurrent = 0.0f;
    float mockDistanceCm = 999.0f;



public:
    MockHardware() {}
    virtual ~MockHardware() {}

    // HardwareInterface implementation
    void setPinMode(uint8_t pin, uint8_t mode) override {
        pinModes[pin] = mode;
        eventHistory.push_back({EVENT_MODE, pin, mode, mockMillis});
    }
    
    void writePin(uint8_t pin, uint8_t val) override {
        pinValues[pin] = val;
        eventHistory.push_back({EVENT_WRITE, pin, val, mockMillis});
    }
    
    int readPin(uint8_t pin) override {
        if (pinValues.find(pin) != pinValues.end()) {
            return pinValues[pin];
        }
        return LOW;
    }
    
    unsigned long getMillis() override {
        return mockMillis;
    }
    
    void delayMs(unsigned long ms) override {
        mockMillis += ms;
    }
    
    float getBatteryVoltage() override {
        return mockBatteryVoltage;
    }
    
    float getMotorCurrent() override {
        return mockMotorCurrent;
    }
    
    float getDistanceCm() override {
        return mockDistanceCm;
    }

    // Mock Helper Methods
    void advanceMillis(unsigned long ms) {
        mockMillis += ms;
    }
    
    void setBatteryVoltage(float v) {
        mockBatteryVoltage = v;
    }
    
    void setMotorCurrent(float c) {
        mockMotorCurrent = c;
    }
    
    void setDistanceCm(float d) {
        mockDistanceCm = d;
    }
    
    uint8_t getPinMode(uint8_t pin) {
        if (pinModes.find(pin) != pinModes.end()) {
            return pinModes[pin];
        }
        return INPUT; // Default uninitialized pin state is INPUT (high impedance)
    }
    
    uint8_t getPinValue(uint8_t pin) {
        if (pinValues.find(pin) != pinValues.end()) {
            return pinValues[pin];
        }
        return LOW;
    }
    
    const std::vector<PinEvent>& getEventHistory() const {
        return eventHistory;
    }
    
    void clearEventHistory() {
        eventHistory.clear();
    }
    
    // Clear pin state logs
    void reset() {
        pinModes.clear();
        pinValues.clear();
        eventHistory.clear();
        mockMillis = 0;
        mockBatteryVoltage = 12.0f;
        mockMotorCurrent = 0.0f;
        mockDistanceCm = 999.0f;
    }


};

#endif // MOCK_HARDWARE_H
