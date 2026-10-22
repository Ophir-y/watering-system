#include "NodeHw.h"

#include <algorithm>

#include "boards.h"

namespace {
constexpr int kSoilSamples = 9;
constexpr uint32_t kSoilWarmupMs = 300;
constexpr uint32_t kCutoffMarginMs = 5000;
constexpr uint32_t kLongPressMs = 5000;

uint32_t medianMv(int pin, int n) {
  uint32_t v[15];
  n = std::min(n, 15);
  for (int i = 0; i < n; ++i) v[i] = analogReadMilliVolts(pin);
  std::sort(v, v + n);
  return v[n / 2];
}
}  // namespace

void NodeHw::forceSafe() {
  pinMode(board::kPump, OUTPUT);
  digitalWrite(board::kPump, LOW);
  pinMode(board::kSoilPower, OUTPUT);
  digitalWrite(board::kSoilPower, LOW);
}

void NodeHw::begin() {
  forceSafe();
  pinMode(board::kButton, INPUT_PULLUP);
  if (board::kLed >= 0) pinMode(board::kLed, OUTPUT);
  led(false);
  analogReadResolution(12);
  analogSetPinAttenuation(board::kSoil, ADC_11db);
  analogSetPinAttenuation(board::kBattery, ADC_11db);
  analogSetPinAttenuation(board::kPumpSense, ADC_0db);  // a few hundred mV at most: best resolution

  esp_timer_create_args_t args = {};
  args.callback = &NodeHw::cutoff;
  args.arg = this;
  args.name = "pump-cutoff";
  esp_timer_create(&args, &cutoffTimer_);
}

void IRAM_ATTR NodeHw::cutoff(void* arg) {
  digitalWrite(board::kPump, LOW);
  static_cast<NodeHw*>(arg)->pumpOn_ = false;
}

void NodeHw::on() {
  if (cutoffTimer_) {
    esp_timer_stop(cutoffTimer_);
    esp_timer_start_once(cutoffTimer_, (uint64_t(hardLimitMs_) + kCutoffMarginMs) * 1000);
  }
  pumpOn_ = true;
  digitalWrite(board::kPump, HIGH);
}

void NodeHw::off() {
  digitalWrite(board::kPump, LOW);
  pumpOn_ = false;
  if (cutoffTimer_) esp_timer_stop(cutoffTimer_);
}

bool NodeHw::readSoil(int32_t& raw) {
  digitalWrite(board::kSoilPower, HIGH);
  delay(kSoilWarmupMs);
  const uint32_t mv = medianMv(board::kSoil, kSoilSamples);
  digitalWrite(board::kSoilPower, LOW);
  raw = static_cast<int32_t>(mv * 8);
  return true;  // out-of-range values (unplugged, shorted) are judged by the sensor monitor
}

float NodeHw::batteryMv() { return medianMv(board::kBattery, 7) * board::kBatteryDivider; }

float NodeHw::pumpMa() { return medianMv(board::kPumpSense, 5) / board::kShuntOhms; }

bool NodeHw::buttonDown() const { return digitalRead(board::kButton) == LOW; }

NodeHw::Press NodeHw::pollButton() {
  if (!buttonDown()) return Press::None;
  const uint32_t t0 = millis();
  while (buttonDown()) {
    if (millis() - t0 >= kLongPressMs) {
      flash(5);
      while (buttonDown()) delay(10);
      return Press::Long;
    }
    delay(10);
  }
  return millis() - t0 >= 30 ? Press::Short : Press::None;
}

void NodeHw::led(bool on) {
  if (board::kLed < 0) return;
  digitalWrite(board::kLed, on != board::kLedActiveLow ? HIGH : LOW);
}

void NodeHw::flash(uint8_t times) {
  for (uint8_t i = 0; i < times; ++i) {
    led(true);
    delay(60);
    led(false);
    delay(120);
  }
}

void NodeHw::tick() { led(blinking() && millis() % 500 < 250); }
