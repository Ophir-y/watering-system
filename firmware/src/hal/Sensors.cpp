#include "Sensors.h"

#include <algorithm>

#include "Signal.h"
#include "config.h"
#include "pins.h"

// ------------------------------------------------------------------ moisture

void MoistureBank::begin(TwoWire& wire) {
  wire_ = &wire;
  for (int i = 0; i < 2; ++i) ads_[i].begin(wire, pins::kAdsAddress[i]);
}

bool MoistureBank::read(uint8_t channel, int32_t& raw) {
  if (channel >= 8 || !wire_) return false;
  Ads1115& ads = ads_[channel / 4];
  if (!ads.present() && !ads.begin(*wire_, pins::kAdsAddress[channel / 4])) return false;
  int32_t samples[config::kMoistureSamples];
  for (int i = 0; i < config::kMoistureSamples; ++i) {
    int16_t v = 0;
    if (!ads.read(channel % 4, v)) return false;
    samples[i] = v;
  }
  raw = plantcore::medianInPlace(samples, config::kMoistureSamples);
  return true;
}

// ------------------------------------------------------------------ environment

void EnvSensors::begin(TwoWire& wire) {
  wire_ = &wire;
  sht_.begin(wire, pins::kShtAddress);
  bh_.begin(wire, pins::kBh1750Address);
}

plantcore::EnvSnapshot EnvSensors::read() {
  plantcore::EnvSnapshot env;
  // Retry missing sensors now and then (hot-plugged or a loose connector).
  if (++reads_ % 10 == 0) {
    if (!sht_.present()) sht_.begin(*wire_, pins::kShtAddress);
    if (!bh_.present()) bh_.begin(*wire_, pins::kBh1750Address);
  }
  float t = 0, h = 0, lux = 0;
  if (sht_.present() && sht_.read(t, h)) {
    env.valid = true;
    env.tempC = t;
    env.humidityPct = h;
  }
  if (bh_.present() && bh_.read(lux)) {
    env.luxValid = true;
    env.lux = lux;
  }
  return env;
}

// ------------------------------------------------------------------ scale

void Scale::begin(int doutPin, int sckPin) { hx_.begin(doutPin, sckPin); }

void Scale::setCalibration(int32_t offset, float countsPerGram) {
  offset_ = offset;
  countsPerGram_ = countsPerGram;
}

void Scale::poll(uint32_t nowMs) {
  if (!hx_.is_ready()) return;
  window_[count_ % 3] = static_cast<int32_t>(hx_.read());
  count_++;
  int32_t w[3] = {window_[0], window_[1], window_[2]};
  filtered_ = count_ >= 3 ? plantcore::medianInPlace(w, 3) : window_[(count_ - 1) % 3];
  lastReadMs_ = nowMs;
  haveReading_ = true;
}

bool Scale::ok(uint32_t nowMs) const {
  return calibrated() && haveReading_ && nowMs - lastReadMs_ < config::kScaleStaleMs;
}

float Scale::grams() const {
  if (!calibrated()) return 0;
  return (filtered_ - offset_) / countsPerGram_;
}

bool Scale::averageRaw(int samples, int32_t& out) {
  int64_t sum = 0;
  int got = 0;
  const uint32_t start = millis();
  while (got < samples && millis() - start < 3000) {
    if (hx_.is_ready()) {
      sum += hx_.read();
      got++;
    } else {
      delay(5);
    }
  }
  if (got < samples) return false;
  out = static_cast<int32_t>(sum / got);
  return true;
}
