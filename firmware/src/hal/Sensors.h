// Sensor wrappers: soil moisture (2x ADS1115), environment (SHT4x + BH1750) and
// the reservoir scale (HX711).
#pragma once

#include <Arduino.h>
#include <HX711.h>
#include <Wire.h>

#include "I2cDevices.h"
#include "Types.h"

class MoistureBank {
 public:
  void begin(TwoWire& wire);
  bool chipPresent(int chip) const { return ads_[chip].present(); }
  // Median of several conversions of channel 0..7 (chip = ch / 4, input = ch % 4).
  bool read(uint8_t channel, int32_t& raw);

 private:
  TwoWire* wire_ = nullptr;
  Ads1115 ads_[2];
};

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

class Scale {
 public:
  void begin(int doutPin, int sckPin);
  void setCalibration(int32_t offset, float countsPerGram);
  void poll(uint32_t nowMs);  // non-blocking; call often
  bool ok(uint32_t nowMs) const;
  bool calibrated() const { return countsPerGram_ != 0; }
  float grams() const;
  int32_t raw() const { return filtered_; }
  uint32_t samples() const { return static_cast<uint32_t>(count_); }
  // Averages fresh readings for calibration (blocks up to ~2 s).
  bool averageRaw(int samples, int32_t& out);

 private:
  HX711 hx_;
  int32_t window_[3] = {0, 0, 0};
  int count_ = 0;
  int32_t filtered_ = 0;
  uint32_t lastReadMs_ = 0;
  bool haveReading_ = false;
  int32_t offset_ = 0;
  float countsPerGram_ = 0;
};
