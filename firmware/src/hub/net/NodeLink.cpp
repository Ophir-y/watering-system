#include "NodeLink.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>

#include "../app/Hub.h"

namespace {

struct Frame {
  plantlink::Mac from;
  size_t len;
  uint8_t data[plantlink::kMaxFrame];
};

QueueHandle_t rxQueue = nullptr;

}  // namespace

void NodeLink::onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (!rxQueue || len <= 0 || len > static_cast<int>(plantlink::kMaxFrame)) return;
  Frame f;
  memcpy(f.from.data(), mac, 6);
  f.len = static_cast<size_t>(len);
  memcpy(f.data, data, f.len);
  xQueueSend(rxQueue, &f, 0);
}

void NodeLink::begin(Hub& hub) {
  hub_ = &hub;
  rxQueue = xQueueCreate(8, sizeof(Frame));
  WiFi.setSleep(false);  // a dozing radio misses node frames between router beacons
  if (esp_now_init() != ESP_OK) {
    log_e("ESP-NOW failed to start");
    return;
  }
  esp_now_register_recv_cb(&NodeLink::onRecv);
}

uint8_t NodeLink::channel() const {
  uint8_t primary = 1;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&primary, &second);
  return primary;
}

bool NodeLink::send(const plantlink::Mac& to, const uint8_t* d, size_t n) {
  if (!esp_now_is_peer_exist(to.data())) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, to.data(), 6);
    peer.channel = 0;  // the current channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;  // frames carry their own HMAC
    if (esp_now_add_peer(&peer) != ESP_OK) {
      // The peer table is full: forget the oldest peers, they are re-added on demand.
      esp_now_peer_info_t old;
      if (esp_now_fetch_peer(true, &old) == ESP_OK) esp_now_del_peer(old.peer_addr);
      if (esp_now_add_peer(&peer) != ESP_OK) return false;
    }
  }
  return esp_now_send(to.data(), d, n) == ESP_OK;
}

void NodeLink::task() {
  Frame f;
  uint8_t reply[plantlink::kMaxFrame];
  for (;;) {
    if (xQueueReceive(rxQueue, &f, portMAX_DELAY) != pdTRUE) continue;
    framesIn_++;
    const size_t n = hub_->onFrame(f.from, f.data, f.len, reply);
    if (n) send(f.from, reply, n);
  }
}
