// Pin maps for the plant-node boards. See docs/hardware/wiring.md.
//
// Every board uses the same six connections:
//   soil      capacitive sensor output       (ADC1)
//   soilPower sensor supply, on only while reading
//   pump      MOSFET gate, 100 kΩ pull-down  (never a strapping pin)
//   pumpSense top of the 1 Ω shunt            (ADC1): mV = mA
//   battery   middle of the 1 MΩ / 1 MΩ divider (ADC1)
//   button    the board's BOOT button, active low
#pragma once

#include <stdint.h>

namespace board {

#if defined(BOARD_SUPERMINI_C3)
constexpr uint8_t kId = 1;  // plantlink::Board::SuperMiniC3
constexpr int kSoil = 0, kPumpSense = 1, kBattery = 3, kSoilPower = 4, kPump = 10, kButton = 9;
constexpr int kLed = 8;
constexpr bool kLedActiveLow = true;
constexpr bool kLowTxPower = true;  // these boards' antennas misbehave at full power
#elif defined(BOARD_XIAO_C3)
constexpr uint8_t kId = 2;  // plantlink::Board::XiaoC3
constexpr int kBattery = 2, kSoil = 3, kPumpSense = 4, kSoilPower = 5, kPump = 10, kButton = 9;
constexpr int kLed = -1;  // no user LED
constexpr bool kLedActiveLow = true;
constexpr bool kLowTxPower = false;
#elif defined(BOARD_XIAO_S3)
constexpr uint8_t kId = 3;  // plantlink::Board::XiaoS3
constexpr int kSoil = 1, kPumpSense = 2, kBattery = 4, kSoilPower = 5, kPump = 6, kButton = 0;
constexpr int kLed = 21;
constexpr bool kLedActiveLow = true;
constexpr bool kLowTxPower = false;
#else
#error "Build a node with one of the node_* environments in platformio.ini"
#endif

constexpr float kBatteryDivider = 2.0f;  // 1 MΩ over 1 MΩ
constexpr float kShuntOhms = 1.0f;

}  // namespace board
