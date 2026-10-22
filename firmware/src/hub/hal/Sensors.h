// Room climate for the main station: SHT4x (temperature, humidity) and BH1750 (light).
// Both are optional; the nodes use the readings to adjust watering.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "I2cDevices.h"
#include "Types.h"

class EnvSensors {
 public:
  void begin(TwoWire& wire);
  plantcore::EnvSnapshot read();
  bool climatePresent() const { return sht_.present(); }
  bool lightPresent() const { return bh_.present(); }

 private:
  TwoWire* wire_ = nullptr;
  Sht4x sht_;
  Bh1750 bh_;
  uint32_t reads_ = 0;
};
