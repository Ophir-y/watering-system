// Plant node entry point: the pump is switched off before anything else, then the
// node alternates between short bursts of work and light sleep. Light sleep keeps
// RAM (and so the engine's state) and wakes on a timer or the button.
//
// Build with -DNODE_STAY_AWAKE to keep USB serial alive while debugging.
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "Node.h"
#include "boards.h"

namespace {
Node node;
}

void setup() {
  NodeHw::forceSafe();
  Serial.begin(115200);
  node.begin();
}

void loop() {
  const uint32_t sleepMs = node.step();
  if (sleepMs <= 250) {
    delay(sleepMs);  // pumping or blinking: stay awake
    return;
  }
#ifdef NODE_STAY_AWAKE
  const uint32_t t0 = millis();
  while (millis() - t0 < sleepMs && digitalRead(board::kButton) == HIGH) delay(20);
#else
  Serial.flush();
  esp_sleep_enable_timer_wakeup(uint64_t(sleepMs) * 1000);
  gpio_wakeup_enable(static_cast<gpio_num_t>(board::kButton), GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
#endif
}
