#include "NodeRadio.h"

#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>

#include <cstring>

namespace {
QueueHandle_t rxQueue = nullptr;
volatile bool sendDone = false;
volatile bool sendOk = false;
const plantlink::Mac kBroadcast{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
}  // namespace

void NodeRadio::onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (!rxQueue || len <= 0 || len > static_cast<int>(plantlink::kMaxFrame)) return;
  RadioFrame f;
  memcpy(f.from.data(), mac, 6);
  f.len = static_cast<size_t>(len);
  memcpy(f.data, data, f.len);
  xQueueSend(rxQueue, &f, 0);
}

void NodeRadio::onSent(const uint8_t*, esp_now_send_status_t status) {
  sendOk = status == ESP_NOW_SEND_SUCCESS;
  sendDone = true;
}

void NodeRadio::begin(bool lowTxPower) {
  lowTx_ = lowTxPower;
  if (!rxQueue) rxQueue = xQueueCreate(4, sizeof(RadioFrame));
  esp_read_mac(mac_.data(), ESP_MAC_WIFI_STA);
}

void NodeRadio::up(uint8_t channel) {
  if (!up_) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);  // never join a network: ESP-NOW only
    if (lowTx_) WiFi.setTxPower(WIFI_POWER_8_5dBm);
    esp_now_init();
    esp_now_register_recv_cb(&NodeRadio::onRecv);
    esp_now_register_send_cb(&NodeRadio::onSent);
    up_ = true;
  }
  setChannel(channel);
  flush();
}

void NodeRadio::down() {
  if (!up_) return;
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  up_ = false;
}

void NodeRadio::setChannel(uint8_t channel) {
  channel_ = channel;
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void NodeRadio::ensurePeer(const plantlink::Mac& mac) {
  if (esp_now_is_peer_exist(mac.data())) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac.data(), 6);
  peer.channel = 0;  // whatever channel the radio is on
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;  // frames carry their own HMAC
  esp_now_add_peer(&peer);
}

bool NodeRadio::send(const plantlink::OutFrame& f) {
  if (!up_ || f.len == 0) return false;
  const plantlink::Mac& to = f.broadcast ? kBroadcast : f.to;
  ensurePeer(to);
  sendDone = false;
  if (esp_now_send(to.data(), f.data, f.len) != ESP_OK) return false;
  const uint32_t t0 = millis();
  while (!sendDone && millis() - t0 < 50) delay(1);
  return sendDone && sendOk;
}

bool NodeRadio::receive(RadioFrame& out, uint32_t timeoutMs) {
  return rxQueue && xQueueReceive(rxQueue, &out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void NodeRadio::flush() {
  RadioFrame f;
  while (rxQueue && xQueueReceive(rxQueue, &f, 0) == pdTRUE) {
  }
}
