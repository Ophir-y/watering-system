// ESP-NOW on the main station, next to its Wi-Fi connection: it listens on the home
// router's channel (or the setup hotspot's) and answers every node frame at once.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Frames.h"

class Hub;

class NodeLink {
 public:
  void begin(Hub& hub);  // after Wi-Fi has started
  [[noreturn]] void task();
  uint8_t channel() const;
  uint32_t framesIn() const { return framesIn_; }

 private:
  static void onRecv(const uint8_t* mac, const uint8_t* data, int len);
  bool send(const plantlink::Mac& to, const uint8_t* d, size_t n);

  Hub* hub_ = nullptr;
  uint32_t framesIn_ = 0;
};
