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
// Historical triggered model retained for regression characterization only.
// Production Esp32Hardware uses the field-baseline continuous path below.
PowerReading sampleIna219(Ina219Bus& bus);
// Production rollback: use the same continuous32V/2A calibration as field e0323a0.
// Each vendor read must report success; no dummy nominal values are permitted.
class Ina219ContinuousSource {
 public:
  virtual ~Ina219ContinuousSource()=default;
  virtual bool readConfiguration(uint16_t &value)=0;
  virtual bool readCalibration(uint16_t &value)=0;
  virtual bool readBusVoltage(float &value)=0;
  virtual bool readShuntVoltageMv(float &value)=0;
  virtual bool readCurrentMa(float &value)=0;
};
PowerReading sampleIna219Continuous(Ina219ContinuousSource& source);
#endif
