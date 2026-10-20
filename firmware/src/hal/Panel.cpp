#include "Panel.h"

#include "config.h"
#include "pins.h"

namespace {
constexpr uint8_t kDebounceSamples = 3;  // 150 ms at 50 ms polling
constexpr uint32_t kShortPressMinMs = 50;
constexpr uint32_t kShortPressMaxMs = 2000;
constexpr uint32_t kLongPressMs = 5000;
constexpr uint32_t kAlarmEveryMs = 10000;
constexpr uint32_t kBeepMs = 150;
}  // namespace

bool PanelInputs::Debounced::update(bool raw) {
  if (raw == state) {
    count = 0;
    return false;
  }
  if (raw != candidate) {
    candidate = raw;
    count = 1;
    return false;
  }
  if (++count >= kDebounceSamples) {
    state = raw;
    count = 0;
    return true;
  }
  return false;
}

void PanelInputs::begin() {
  pinMode(pins::kFloatSwitch, INPUT);  // external pull-ups on input-only pins
  pinMode(pins::kLeakSensor, INPUT);
  pinMode(pins::kButton, INPUT_PULLUP);
  // Start from the real state so boot does not see a phantom change.
  float_.state = digitalRead(pins::kFloatSwitch) == (config::kFloatOpenMeansLow ? HIGH : LOW);
  leakIn_.state = digitalRead(pins::kLeakSensor) == (config::kLeakActiveLow ? LOW : HIGH);
  floatLow_ = float_.state;
  leak_ = leakIn_.state;
}

void PanelInputs::poll(uint32_t nowMs) {
  float_.update(digitalRead(pins::kFloatSwitch) == (config::kFloatOpenMeansLow ? HIGH : LOW));
  leakIn_.update(digitalRead(pins::kLeakSensor) == (config::kLeakActiveLow ? LOW : HIGH));
  floatLow_ = float_.state;
  leak_ = leakIn_.state;

  const bool down = digitalRead(pins::kButton) == LOW;
  if (down && !buttonDown_) {
    pressedAt_ = nowMs;
    longFired_ = false;
  } else if (down && !longFired_ && nowMs - pressedAt_ >= kLongPressMs) {
    longFired_ = true;
    pending_ = Press::Long;
  } else if (!down && buttonDown_ && !longFired_) {
    const uint32_t held = nowMs - pressedAt_;
    if (held >= kShortPressMinMs && held <= kShortPressMaxMs) pending_ = Press::Short;
  }
  buttonDown_ = down;
}

PanelInputs::Press PanelInputs::takePress() {
  Press p = pending_;
  pending_ = Press::None;
  return p;
}

// ------------------------------------------------------------------ indicators

void Indicators::begin() {
  pinMode(pins::kStatusLed, OUTPUT);
  pinMode(pins::kBuzzer, OUTPUT);
  digitalWrite(pins::kStatusLed, HIGH);
  digitalWrite(pins::kBuzzer, LOW);
}

void Indicators::chirp(uint8_t count) {
  chirps_ = count;
  chirpAt_ = 0;
}

void Indicators::update(uint32_t nowMs) {
  bool led = false;
  switch (pattern_) {
    case LedPattern::Booting:
    case LedPattern::Stopped: led = true; break;                         // solid
    case LedPattern::Ok: led = nowMs % 3000 < 60; break;                 // heartbeat
    case LedPattern::Warning: led = nowMs % 1000 < 500; break;           // 1 Hz
    case LedPattern::Critical: led = nowMs % 250 < 125; break;           // 4 Hz
    case LedPattern::SetupAp: {                                          // double flash
      uint32_t t = nowMs % 1000;
      led = t < 80 || (t > 200 && t < 280);
      break;
    }
  }
  digitalWrite(pins::kStatusLed, led ? HIGH : LOW);

  // Buzzer: queued chirps first, then periodic alarm beeps.
  if (buzzerOn_ && nowMs >= buzzerOffAt_) {
    buzzerOn_ = false;
    digitalWrite(pins::kBuzzer, LOW);
  }
  if (buzzerOn_) return;
  if (chirps_ > 0 && nowMs >= chirpAt_) {
    chirps_--;
    chirpAt_ = nowMs + 2 * kBeepMs;
    buzzerOn_ = true;
    buzzerOffAt_ = nowMs + kBeepMs / 2;
    digitalWrite(pins::kBuzzer, HIGH);
  } else if (alarm_ && nowMs - lastAlarmAt_ >= kAlarmEveryMs) {
    lastAlarmAt_ = nowMs;
    buzzerOn_ = true;
    buzzerOffAt_ = nowMs + kBeepMs;
    digitalWrite(pins::kBuzzer, HIGH);
  }
}
