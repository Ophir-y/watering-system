#include "NodeStore.h"

#include <ArduinoJson.h>

#include <cstring>

#include "Json.h"

namespace {

constexpr uint8_t kPairingVersion = 1;
constexpr size_t kPairingLen = 1 + 1 + 6 + 1 + 1 + 16 + 4 + 2 + 4 + 2;

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

}  // namespace

void NodeStore::begin() { prefs_.begin("node", false); }

bool NodeStore::loadPairing(plantlink::Pairing& p) {
  uint8_t b[kPairingLen];
  if (prefs_.getBytes("pair", b, sizeof b) != sizeof b || b[0] != kPairingVersion) return false;
  size_t i = 1;
  p.haveHub = b[i++] != 0;
  memcpy(p.hub.data(), b + i, 6);
  i += 6;
  p.channel = b[i++];
  p.paired = b[i++] != 0;
  memcpy(p.key.data(), b + i, 16);
  i += 16;
  p.seq = get32(b + i);
  i += 4;
  p.lastCmdId = static_cast<uint16_t>(b[i] | (b[i + 1] << 8));
  i += 2;
  p.cfgVersion = get32(b + i);
  i += 4;
  p.checkInS = static_cast<uint16_t>(b[i] | (b[i + 1] << 8));
  if (p.channel < 1 || p.channel > 13) p.channel = 1;
  return true;
}

void NodeStore::savePairing(const plantlink::Pairing& p) {
  uint8_t b[kPairingLen];
  size_t i = 0;
  b[i++] = kPairingVersion;
  b[i++] = p.haveHub ? 1 : 0;
  memcpy(b + i, p.hub.data(), 6);
  i += 6;
  b[i++] = p.channel;
  b[i++] = p.paired ? 1 : 0;
  memcpy(b + i, p.key.data(), 16);
  i += 16;
  put32(b + i, p.seq);
  i += 4;
  b[i++] = static_cast<uint8_t>(p.lastCmdId);
  b[i++] = static_cast<uint8_t>(p.lastCmdId >> 8);
  put32(b + i, p.cfgVersion);
  i += 4;
  b[i++] = static_cast<uint8_t>(p.checkInS);
  b[i++] = static_cast<uint8_t>(p.checkInS >> 8);
  prefs_.putBytes("pair", b, sizeof b);
}

bool NodeStore::loadConfig(std::string& json) {
  if (!prefs_.isKey("cfg")) return false;
  json = prefs_.getString("cfg").c_str();
  return !json.empty();
}

void NodeStore::saveConfig(const std::string& json) { prefs_.putString("cfg", json.c_str()); }

void NodeStore::clearConfig() { prefs_.remove("cfg"); }

bool NodeStore::loadState(plantcore::PersistedState& s) {
  if (!prefs_.isKey("state")) return false;
  JsonDocument doc;
  if (deserializeJson(doc, prefs_.getString("state").c_str())) return false;
  return plantcore::fromJson(doc.as<JsonObjectConst>(), s);
}

void NodeStore::saveState(const plantcore::PersistedState& s) {
  JsonDocument doc;
  plantcore::toJson(s, doc.to<JsonObject>());
  std::string text;
  serializeJson(doc, text);
  prefs_.putString("state", text.c_str());
}

void NodeStore::eraseAll() { prefs_.clear(); }
