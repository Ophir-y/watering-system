// Minimal drivers for the I2C parts. Every wait is bounded, so a disconnected or
// stuck device produces an error result instead of hanging a task.
#pragma once

#include <Arduino.h>
#include <Wire.h>

// One lock for the shared bus (the RTC is written from the network task).
class I2cLock {
 public:
  static void init();
  explicit I2cLock(uint32_t timeoutMs = 200);
  ~I2cLock();
  bool ok() const { return ok_; }

 private:
  bool ok_;
};

class Sht4x {
 public:
  bool begin(TwoWire& wire, uint8_t address);
  bool present() const { return present_; }
  bool read(float& tempC, float& humidityPct);

 private:
  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  bool present_ = false;
};

class Bh1750 {
 public:
  bool begin(TwoWire& wire, uint8_t address);
  bool present() const { return present_; }
  bool read(float& lux);

 private:
  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  bool present_ = false;
};

class Ds3231 {
 public:
  bool begin(TwoWire& wire, uint8_t address);
  bool present() const { return present_; }
  // UTC seconds. Returns false if the RTC is absent or lost power (oscillator stopped).
  bool read(int64_t& epoch);
  bool write(int64_t epoch);
  bool lostPower() const { return lostPower_; }

 private:
  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  bool present_ = false;
  bool lostPower_ = false;
};
