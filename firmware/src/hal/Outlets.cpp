#include "Outlets.h"

#include "config.h"
#include "pins.h"

void Outlets::forceSafe() {
  pinMode(pins::kActuatorRail, OUTPUT);
  digitalWrite(pins::kActuatorRail, LOW);
  for (int pin : pins::kOutlets) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
}

void Outlets::begin() {
  forceSafe();
  esp_timer_create_args_t args = {};
  args.callback = &Outlets::onCutoff;
  args.arg = this;
  args.name = "outlet_cutoff";
  esp_timer_create(&args, &timer_);
}

void Outlets::allOff() {
  // Rail first, so nothing can flow while the channels change.
  digitalWrite(pins::kActuatorRail, LOW);
  for (int pin : pins::kOutlets) digitalWrite(pin, LOW);
  open_ = -1;
}

void Outlets::open(uint8_t channel) {
  if (channel >= 8) return;
  portENTER_CRITICAL(&mux_);
  allOff();
  digitalWrite(pins::kOutlets[channel], HIGH);
  digitalWrite(pins::kActuatorRail, HIGH);
  open_ = channel;
  portEXIT_CRITICAL(&mux_);
  if (timer_) {
    esp_timer_stop(timer_);
    esp_timer_start_once(timer_, static_cast<uint64_t>(limitMs_ + config::kHardCutoffMarginMs) * 1000ULL);
  }
}

void Outlets::closeAll() {
  portENTER_CRITICAL(&mux_);
  allOff();
  portEXIT_CRITICAL(&mux_);
  if (timer_) esp_timer_stop(timer_);
}

void Outlets::onCutoff(void* arg) {
  auto* self = static_cast<Outlets*>(arg);
  portENTER_CRITICAL(&self->mux_);
  bool wasOpen = self->open_ >= 0;
  self->allOff();
  portEXIT_CRITICAL(&self->mux_);
  if (wasOpen) self->cutoffs_ = self->cutoffs_ + 1;
}
