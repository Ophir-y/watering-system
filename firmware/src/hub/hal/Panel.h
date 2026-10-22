// The main station's panel: the BOOT button in, the status LED out. Polled from
// the network task.
//   short press  check the LED pattern (no action)
//   hold 5 s     open the setup hotspot
#pragma once

#include <Arduino.h>

class PanelButton {
 public:
  enum class Press : uint8_t { None, Short, Long };
  void begin();
  void poll(uint32_t nowMs);
  Press takePress();

 private:
  bool down_ = false;
  bool longFired_ = false;
  uint32_t pressedAt_ = 0;
  Press pending_ = Press::None;
};

enum class LedPattern : uint8_t { Booting, Ok, Warning, Critical, Stopped, SetupAp };

class StatusLed {
 public:
  void begin();
  void setPattern(LedPattern p) { pattern_ = p; }
  void update(uint32_t nowMs);

 private:
  LedPattern pattern_ = LedPattern::Booting;
};
