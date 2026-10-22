// The plant node's hardware: pump (with an independent hardware-timer cut-off),
// soil sensor (powered only while it is read), pump current, battery, button, LED.
#pragma once

#include <Arduino.h>
#include <esp_timer.h>

#include "Dose.h"

class NodeHw : public plantcore::IPump {
 public:
  // Pump off and pins safe: call first thing at boot.
  static void forceSafe();
  void begin();

  // IPump. on() also arms a hardware timer that switches the pump off after
  // hardLimitMs + a margin, whatever the software does.
  void on() override;
  void off() override;
  bool pumpOn() const { return pumpOn_; }
  void setHardLimitMs(uint32_t ms) { hardLimitMs_ = ms; }

  // Median of several readings; raw = millivolts x 8. Takes ~300 ms (sensor warm-up).
  bool readSoil(int32_t& raw);
  float batteryMv();
  float pumpMa();

  enum class Press : uint8_t { None, Short, Long };
  // Call after waking: measures how long the button is held (up to the long press).
  Press pollButton();
  bool buttonDown() const;

  // Blinks for `ms` (board LED, if it has one). Updated by tick().
  void blink(uint32_t ms) { blinkUntil_ = millis() + ms; }
  void flash(uint8_t times);  // short acknowledgement, blocking
  void tick();
  bool blinking() const { return static_cast<int32_t>(blinkUntil_ - millis()) > 0; }

 private:
  static void IRAM_ATTR cutoff(void*);
  void led(bool on);

  esp_timer_handle_t cutoffTimer_ = nullptr;
  uint32_t hardLimitMs_ = 120000;
  volatile bool pumpOn_ = false;
  uint32_t blinkUntil_ = 0;
};
