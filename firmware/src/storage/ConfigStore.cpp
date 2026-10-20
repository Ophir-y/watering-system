#include "ConfigStore.h"

#include <ArduinoJson.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include "Json.h"
#include "config.h"

namespace {

constexpr const char* kNamespace = "plantcare";
constexpr int kHashRounds = 2000;

class Guard {
 public:
  explicit Guard(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(m_); }

 private:
  SemaphoreHandle_t m_;
};

std::string toHex(const uint8_t* data, size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0x0F];
  }
  return out;
}

std::string hashPassword(const std::string& salt, const std::string& password) {
  uint8_t digest[32];
  std::string input = salt + password;
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(input.data()), input.size(), digest, 0);
  for (int i = 1; i < kHashRounds; ++i) mbedtls_sha256_ret(digest, sizeof digest, digest, 0);
  return toHex(digest, sizeof digest);
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  return diff == 0;
}

}  // namespace

bool ConfigStore::begin() {
  mutex_ = xSemaphoreCreateMutex();
  return prefs_.begin(kNamespace, false);
}

bool ConfigStore::putText(const char* key, const std::string& text) {
  return prefs_.putBytes(key, text.data(), text.size()) == text.size();
}

bool ConfigStore::getText(const char* key, std::string& text) {
  size_t len = prefs_.getBytesLength(key);
  if (len == 0) return false;
  text.assign(len, '\0');
  return prefs_.getBytes(key, &text[0], len) == len;
}

bool ConfigStore::loadSettings(plantcore::Settings& s) {
  Guard g(mutex_);
  std::string text;
  if (!getText("settings", text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  plantcore::Settings parsed;
  std::string err;
  if (!plantcore::fromJson(doc.as<JsonObjectConst>(), parsed, err)) return false;
  parsed.notify.token = prefs_.getString("ntfy_token", "").c_str();
  s = parsed;
  return true;
}

bool ConfigStore::saveSettings(const plantcore::Settings& s) {
  Guard g(mutex_);
  JsonDocument doc;
  plantcore::toJson(s, doc.to<JsonObject>());
  std::string text;
  serializeJson(doc, text);
  bool ok = putText("settings", text);
  prefs_.putString("ntfy_token", s.notify.token.c_str());
  return ok;
}

bool ConfigStore::loadPlants(std::vector<plantcore::PlantProfile>& plants) {
  Guard g(mutex_);
  std::string text;
  plants.clear();
  if (!getText("plants", text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    plantcore::PlantProfile p;
    std::string err;
    if (plantcore::fromJson(o, p, err)) {
      plants.push_back(p);
    } else {
      log_w("Skipping stored plant: %s", err.c_str());
    }
  }
  return true;
}

bool ConfigStore::savePlants(const std::vector<plantcore::PlantProfile>& plants) {
  Guard g(mutex_);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& p : plants) plantcore::toJson(p, arr.add<JsonObject>());
  std::string text;
  serializeJson(doc, text);
  return putText("plants", text);
}

bool ConfigStore::loadState(plantcore::PersistedState& s) {
  Guard g(mutex_);
  std::string text;
  if (!getText("state", text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  return plantcore::fromJson(doc.as<JsonObjectConst>(), s);
}

bool ConfigStore::saveState(const plantcore::PersistedState& s) {
  Guard g(mutex_);
  JsonDocument doc;
  plantcore::toJson(s, doc.to<JsonObject>());
  std::string text;
  serializeJson(doc, text);
  return putText("state", text);
}

ConfigStore::WifiConfig ConfigStore::loadWifi() {
  Guard g(mutex_);
  WifiConfig w;
  w.ssid = prefs_.getString("wifi_ssid", "").c_str();
  w.password = prefs_.getString("wifi_pass", "").c_str();
  w.apPassword = prefs_.getString("ap_pass", config::kDefaultApPassword).c_str();
  return w;
}

bool ConfigStore::saveWifi(const WifiConfig& w) {
  Guard g(mutex_);
  prefs_.putString("wifi_ssid", w.ssid.c_str());
  prefs_.putString("wifi_pass", w.password.c_str());
  return prefs_.putString("ap_pass", w.apPassword.c_str()) > 0;
}

ConfigStore::ScaleCal ConfigStore::loadScale() {
  Guard g(mutex_);
  ScaleCal c;
  c.offset = prefs_.getInt("scale_off", 0);
  c.countsPerGram = prefs_.getFloat("scale_cpg", 0);
  return c;
}

bool ConfigStore::saveScale(const ScaleCal& c) {
  Guard g(mutex_);
  prefs_.putInt("scale_off", c.offset);
  return prefs_.putFloat("scale_cpg", c.countsPerGram) > 0;
}

bool ConfigStore::hasPassword() {
  Guard g(mutex_);
  return prefs_.isKey("pw_hash");
}

bool ConfigStore::checkPassword(const std::string& password) {
  Guard g(mutex_);
  std::string salt = prefs_.getString("pw_salt", "").c_str();
  std::string hash = prefs_.getString("pw_hash", "").c_str();
  if (hash.empty()) return false;
  return constantTimeEquals(hashPassword(salt, password), hash);
}

bool ConfigStore::setPassword(const std::string& password) {
  Guard g(mutex_);
  uint8_t saltBytes[16];
  esp_fill_random(saltBytes, sizeof saltBytes);
  std::string salt = toHex(saltBytes, sizeof saltBytes);
  prefs_.putString("pw_salt", salt.c_str());
  return prefs_.putString("pw_hash", hashPassword(salt, password).c_str()) > 0;
}

std::vector<std::string> ConfigStore::loadTokenHashes() {
  Guard g(mutex_);
  std::vector<std::string> out;
  std::string all = prefs_.getString("tokens", "").c_str();
  size_t start = 0;
  while (start < all.size()) {
    size_t comma = all.find(',', start);
    if (comma == std::string::npos) comma = all.size();
    if (comma > start) out.push_back(all.substr(start, comma - start));
    start = comma + 1;
  }
  return out;
}

bool ConfigStore::saveTokenHashes(const std::vector<std::string>& hashes) {
  Guard g(mutex_);
  std::string all;
  for (const auto& h : hashes) {
    if (!all.empty()) all += ',';
    all += h;
  }
  return prefs_.putString("tokens", all.c_str()) == all.size();
}

void ConfigStore::factoryReset() {
  Guard g(mutex_);
  prefs_.clear();
}
