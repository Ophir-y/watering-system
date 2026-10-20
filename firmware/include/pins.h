// Pin map for the ESP32 DevKit V1 (WROOM-32). See docs/hardware/wiring.md.
#pragma once

#include <stdint.h>

namespace pins {

// I2C bus: 2x ADS1115 (0x48, 0x49), SHT4x (0x44), BH1750 (0x23), DS3231 (0x68)
constexpr int kI2cSda = 21;
constexpr int kI2cScl = 22;

// microSD (VSPI)
constexpr int kSdCs = 5;
constexpr int kSdSck = 18;
constexpr int kSdMiso = 19;
constexpr int kSdMosi = 23;

// HX711 load-cell amplifier under the reservoir
constexpr int kScaleDout = 34;  // input-only pin
constexpr int kScaleSck = 4;

// Digital inputs (34-39 are input-only and have no internal pull-ups)
constexpr int kFloatSwitch = 35;  // 10k pull-up to 3V3; contact open = water low (fail-safe)
constexpr int kLeakSensor = 36;   // comparator output, LOW = wet; 10k pull-up to 3V3
constexpr int kButton = 0;        // on-board BOOT button, LOW = pressed

// Outlets: one logic-level MOSFET (pump) or valve driver per channel, 10k gate pull-down each.
// Channels 7-8 use GPIO14/15, which glitch at boot; harmless because the rail is off then.
constexpr int kOutlets[8] = {16, 17, 25, 26, 27, 32, 14, 15};
// Actuator rail: master MOSFET (pumps) or master valve (gravity). Water can only flow
// when the rail AND a channel are both on.
constexpr int kActuatorRail = 33;

constexpr int kBuzzer = 13;     // active buzzer via NPN transistor
constexpr int kStatusLed = 2;   // on-board LED

constexpr uint8_t kAdsAddress[2] = {0x48, 0x49};
constexpr uint8_t kShtAddress = 0x44;
constexpr uint8_t kBh1750Address = 0x23;
constexpr uint8_t kRtcAddress = 0x68;

}  // namespace pins
