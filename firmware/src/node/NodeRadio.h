// ESP-NOW for the plant node. The radio is only on during a check-in: up() before,
// down() after, so it draws nothing while the node sleeps.
#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "NodeSession.h"

struct RadioFrame {
  plantlink::Mac from{};
  size_t len = 0;
  uint8_t data[plantlink::kMaxFrame];
};

class NodeRadio {
 public:
  void begin(bool lowTxPower);
  void up(uint8_t channel);
  void down();
  void setChannel(uint8_t channel);
  bool isUp() const { return up_; }
  // Waits for the link-layer result (unicast: the station's radio acknowledged it).
  bool send(const plantlink::OutFrame& f);
  bool receive(RadioFrame& out, uint32_t timeoutMs);
  void flush();
  plantlink::Mac mac() const { return mac_; }

 private:
  static void onRecv(const uint8_t* mac, const uint8_t* data, int len);
  static void onSent(const uint8_t* mac, esp_now_send_status_t status);
  void ensurePeer(const plantlink::Mac& mac);

  plantlink::Mac mac_{};
  bool lowTx_ = false;
  bool up_ = false;
  uint8_t channel_ = 1;
};
