// Local panel: float switch, leak sensor and button in; status LED and buzzer out.
// Polled from the safety task every 50 ms.
#pragma once

#include <Arduino.h>

class PanelInputs {
 public:
  enum class Press : uint8_t { None, Short, Long };

  void begin();
  void poll(uint32_t nowMs);
  bool floatLow() const { return floatLow_; }
  bool leak() const { return leak_; }
  Press takePress();

 private:
  struct Debounced {
    bool state = false;
    bool candidate = false;
    uint8_t count = 0;
    bool update(bool raw);  // returns true when the debounced state changed
  };
  Debounced float_, leakIn_;
  bool floatLow_ = false;
  bool leak_ = false;
  bool buttonDown_ = false;
  bool longFired_ = false;
  uint32_t pressedAt_ = 0;
  volatile Press pending_ = Press::None;
};

enum class LedPattern : uint8_t { Booting, Ok, Warning, Critical, Stopped, SetupAp };

class Indicators {
 public:
  void begin();
  void setPattern(LedPattern p) { pattern_ = p; }
  // Periodic alarm beeps for unacknowledged critical alerts (caller handles quiet hours).
  void setAlarm(bool on) { alarm_ = on; }
  void chirp(uint8_t count);
  void update(uint32_t nowMs);

 private:
  LedPattern pattern_ = LedPattern::Booting;
  bool alarm_ = false;
  uint8_t chirps_ = 0;
  uint32_t chirpAt_ = 0;
  uint32_t lastAlarmAt_ = 0;
  bool buzzerOn_ = false;
  uint32_t buzzerOffAt_ = 0;
};
