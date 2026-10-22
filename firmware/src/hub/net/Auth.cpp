#include "Auth.h"

#include <Arduino.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include <algorithm>

#include "../storage/ConfigStore.h"
#include "config.h"

namespace {

std::string hex(const uint8_t* data, size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < len; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0x0F];
  }
  return out;
}

std::string sha256Hex(const std::string& s) {
  uint8_t digest[32];
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(s.data()), s.size(), digest, 0);
  return hex(digest, sizeof digest);
}

class Guard {
 public:
  explicit Guard(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(m_); }

 private:
  SemaphoreHandle_t m_;
};

}  // namespace

void Auth::begin(ConfigStore& store) {
  store_ = &store;
  mutex_ = xSemaphoreCreateMutex();
  hashes_ = store.loadTokenHashes();
}

bool Auth::setupRequired() { return !store_->hasPassword(); }

uint32_t Auth::lockedForMs() const {
  const uint32_t now = millis();
  return static_cast<int32_t>(lockedUntilMs_ - now) > 0 ? lockedUntilMs_ - now : 0;
}

std::string Auth::issue() {
  uint8_t raw[16];
  esp_fill_random(raw, sizeof raw);
  const std::string token = hex(raw, sizeof raw);
  hashes_.push_back(sha256Hex(token));
  while (hashes_.size() > static_cast<size_t>(config::kMaxTokens)) hashes_.erase(hashes_.begin());
  store_->saveTokenHashes(hashes_);
  return token;
}

std::string Auth::login(const std::string& password) {
  Guard g(mutex_);
  if (lockedForMs() > 0) return "";
  if (!store_->checkPassword(password)) {
    if (++failures_ >= config::kLoginFailuresBeforeDelay) {
      lockedUntilMs_ = millis() + config::kLoginLockoutMs;
      failures_ = 0;
    }
    return "";
  }
  failures_ = 0;
  return issue();
}

std::string Auth::setup(const std::string& password) {
  Guard g(mutex_);
  if (store_->hasPassword() || password.size() < kMinPasswordLength) return "";
  if (!store_->setPassword(password)) return "";
  hashes_.clear();
  return issue();
}

bool Auth::check(const std::string& token) {
  if (token.size() != 32) return false;
  const std::string h = sha256Hex(token);
  Guard g(mutex_);
  return std::find(hashes_.begin(), hashes_.end(), h) != hashes_.end();
}

void Auth::logout(const std::string& token) {
  const std::string h = sha256Hex(token);
  Guard g(mutex_);
  hashes_.erase(std::remove(hashes_.begin(), hashes_.end(), h), hashes_.end());
  store_->saveTokenHashes(hashes_);
}

bool Auth::changePassword(const std::string& current, const std::string& next, std::string& token) {
  Guard g(mutex_);
  if (next.size() < kMinPasswordLength || !store_->checkPassword(current)) return false;
  if (!store_->setPassword(next)) return false;
  hashes_.clear();  // sign out every other session
  token = issue();
  return true;
}
