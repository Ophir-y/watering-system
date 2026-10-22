// Pin map for the main station: an ESP32 DevKit V1 (WROOM-32). Everything on it is
// optional except the board itself. See docs/hardware/wiring.md.
#pragma once

#include <stdint.h>

namespace pins {

// I2C bus: SHT4x (0x44), BH1750 (0x23), DS3231 (0x68)
constexpr int kI2cSda = 21;
constexpr int kI2cScl = 22;

// microSD (VSPI)
constexpr int kSdCs = 5;
constexpr int kSdSck = 18;
constexpr int kSdMiso = 19;
constexpr int kSdMosi = 23;

constexpr int kButton = 0;     // on-board BOOT button, LOW = pressed
constexpr int kStatusLed = 2;  // on-board LED

constexpr uint8_t kShtAddress = 0x44;
constexpr uint8_t kBh1750Address = 0x23;
constexpr uint8_t kRtcAddress = 0x68;

}  // namespace pins
