#include "Panel.h"

#include "pins.h"

namespace {
constexpr uint32_t kShortPressMinMs = 50;
constexpr uint32_t kShortPressMaxMs = 2000;
constexpr uint32_t kLongPressMs = 5000;
}  // namespace

void PanelButton::begin() { pinMode(pins::kButton, INPUT_PULLUP); }

void PanelButton::poll(uint32_t nowMs) {
  const bool down = digitalRead(pins::kButton) == LOW;
  if (down && !down_) {
    pressedAt_ = nowMs;
    longFired_ = false;
  } else if (down && !longFired_ && nowMs - pressedAt_ >= kLongPressMs) {
    longFired_ = true;
    pending_ = Press::Long;
  } else if (!down && down_ && !longFired_) {
    const uint32_t held = nowMs - pressedAt_;
    if (held >= kShortPressMinMs && held <= kShortPressMaxMs) pending_ = Press::Short;
  }
  down_ = down;
}

PanelButton::Press PanelButton::takePress() {
  const Press p = pending_;
  pending_ = Press::None;
  return p;
}

void StatusLed::begin() {
  pinMode(pins::kStatusLed, OUTPUT);
  digitalWrite(pins::kStatusLed, HIGH);
}

void StatusLed::update(uint32_t nowMs) {
  bool led = false;
  switch (pattern_) {
    case LedPattern::Booting:
    case LedPattern::Stopped: led = true; break;                // solid
    case LedPattern::Ok: led = nowMs % 3000 < 60; break;        // heartbeat
    case LedPattern::Warning: led = nowMs % 1000 < 500; break;  // 1 Hz
    case LedPattern::Critical: led = nowMs % 250 < 125; break;  // 4 Hz
    case LedPattern::SetupAp: {                                 // double flash
      const uint32_t t = nowMs % 1000;
      led = t < 80 || (t > 200 && t < 280);
      break;
    }
  }
  digitalWrite(pins::kStatusLed, led ? HIGH : LOW);
}
