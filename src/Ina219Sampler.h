#ifndef SMARTBOX_INA219_SAMPLER_H
#define SMARTBOX_INA219_SAMPLER_H
#include "HardwareInterface.h"
class Ina219Bus {
 public:
  virtual ~Ina219Bus()=default;
  virtual bool writeRegister(uint8_t reg,uint16_t value)=0;
  virtual bool readRegister(uint8_t reg,uint16_t &value)=0;
  virtual uint32_t nowMs()=0;
  virtual void waitMs(uint32_t ms)=0;
};
// Matches the existing0.1-ohm,100uA/current-LSB profile; no whole-battery energy claim.
PowerReading sampleIna219(Ina219Bus& bus);
#endif
