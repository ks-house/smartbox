#include "Ina219Sampler.h"
#include <cmath>
#include <cstdlib>

PowerReading sampleIna219(Ina219Bus& bus) {
  // TI INA219 RevG sections8.3/8.6:32V,320mV,12-bit shunt+bus triggered.
  // Trigger resets CNVR; unlike continuous reads this qualifies one new cohort.
  constexpr uint16_t profile=0x3998, triggered=profile|3, calibration=4096;
  const auto invalid=[] {return PowerReading{NAN,NAN,false};};
  const auto fail=[&] {bus.writeRegister(0,profile);return invalid();};
  if(!bus.writeRegister(5,calibration) || !bus.writeRegister(0,triggered)) return fail();
  const auto goodProfile=[&] {
    uint16_t config=0,cal=0;
    return bus.readRegister(5,cal) && cal==calibration && bus.readRegister(0,config) &&
      (config&0xfff8)==profile && ((config&7)==3 || (config&7)==0);
  };
  if(!goodProfile()) return fail();
  const uint32_t start=bus.nowMs();uint16_t rawBus=0;bool ready=false;
  while(uint32_t(bus.nowMs()-start)<15) {
    bus.waitMs(1);
    if(!bus.readRegister(2,rawBus)) return fail();
    if(rawBus&2) {ready=true;break;}
  }
  if(!ready || (rawBus&1) || (rawBus&4)) return fail();
  uint16_t rawShunt=0,rawCurrent=0,again=0;
  if(!bus.readRegister(1,rawShunt) || !bus.readRegister(4,rawCurrent) ||
      !goodProfile() || !bus.readRegister(2,again) || again!=rawBus) return fail();
  const int32_t shunt=rawShunt<=32767 ? rawShunt : int32_t(rawShunt)-65536;
  const int32_t current=rawCurrent<=32767 ? rawCurrent : int32_t(rawCurrent)-65536;
  if(std::abs(shunt)>=32000 || std::abs(current)>=32000 || std::abs(current-shunt)>1) return fail();
  const float voltage=float(rawBus>>3)*0.004f+float(shunt)*0.00001f;
  if(!std::isfinite(voltage) || voltage<=0 || voltage>26.0f) return fail();
  if(!bus.writeRegister(0,profile)) return invalid(); // Explicit power-down between samples.
  return {voltage,float(current)*0.1f,true};
}


PowerReading sampleIna219Continuous(Ina219ContinuousSource& source) {
  uint16_t config=0,cal=0;
  const auto fail=[&](PowerReadFault fault) {
    return PowerReading{NAN,NAN,false,fault,1,config,cal};
  };
  const auto profile=[&]() -> PowerReadFault {
    if(!source.readConfiguration(config))return PowerReadFault::CONFIGURATION_IO;
    if(config!=0x399f)return PowerReadFault::PROFILE_MISMATCH;
    if(!source.readCalibration(cal))return PowerReadFault::CALIBRATION_IO;
    if(cal!=4096)return PowerReadFault::CALIBRATION_MISMATCH;
    return PowerReadFault::NONE;
  };
  auto fault=profile();if(fault!=PowerReadFault::NONE)return fail(fault);
  float bus=NAN,shunt=NAN,current=NAN;
  if(!source.readBusVoltage(bus))return fail(PowerReadFault::BUS_IO);
  if(!source.readShuntVoltageMv(shunt))return fail(PowerReadFault::SHUNT_IO);
  if(!source.readCurrentMa(current))return fail(PowerReadFault::CURRENT_IO);
  fault=profile();if(fault!=PowerReadFault::NONE)return fail(fault);
  if(!std::isfinite(bus)||!std::isfinite(shunt)||!std::isfinite(current))return fail(PowerReadFault::NONFINITE);
  const float voltage=bus+shunt/1000.0f;
  if(bus<=0 || bus>26 || voltage<=0 || voltage>26 || std::fabs(shunt)>=320 || std::fabs(current)>=3200)
    return fail(PowerReadFault::RANGE);
  // Continuous registers are read sequentially, as in the working Adafruit path.
  // Do not assert a triggered-cohort equality or estimate whole-battery power.
  return {voltage,current,true,PowerReadFault::NONE,1,config,cal};
}
