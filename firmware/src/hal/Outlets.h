// Pump/valve outputs. Every open() arms an esp_timer that forces everything off
// after the hard on-time limit, independently of the control loop.
#pragma once

#include <Arduino.h>
#include <esp_timer.h>

#include "Dose.h"

class Outlets : public plantcore::IOutlets {
 public:
  // Drive every actuator pin low. Call first thing at boot.
  static void forceSafe();

  void begin();
  void setHardLimitMs(uint32_t ms) { limitMs_ = ms; }

  void open(uint8_t channel) override;
  void closeAll() override;

  int openChannel() const { return open_; }
  uint32_t hardCutoffs() const { return cutoffs_; }

 private:
  static void onCutoff(void* arg);
  void allOff();

  esp_timer_handle_t timer_ = nullptr;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile int open_ = -1;
  volatile uint32_t cutoffs_ = 0;
  uint32_t limitMs_ = 125000;
};
