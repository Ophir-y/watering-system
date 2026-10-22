#include "Sensors.h"

#include "pins.h"

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
