#include "WifiService.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include <algorithm>

#include "../hal/TimeKeeper.h"
#include "config.h"

void WifiService::begin(ConfigStore& store, TimeKeeper& clock) {
  store_ = &store;
  clock_ = &clock;
  cfg_ = store.loadWifi();
  scanMutex_ = xSemaphoreCreateMutex();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof buf, "PlantCare-%02X%02X", mac[4], mac[5]);
  apSsid_ = buf;

  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setHostname(config::kHostname);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  disconnectedSinceMs_ = millis();
  if (configured()) connect();
}

void WifiService::connect() {
  lastAttemptMs_ = millis();
  WiFi.disconnect();
  WiFi.begin(cfg_.ssid.c_str(), cfg_.password.c_str());
}

void WifiService::setCredentials(const std::string& ssid, const std::string& password, const std::string& apPassword) {
  cfg_.ssid = ssid;
  cfg_.password = password;
  if (apPassword.size() >= 8) cfg_.apPassword = apPassword;
  store_->saveWifi(cfg_);
  reconnectRequested_ = true;
}

void WifiService::setAp(bool on) {
  if (on == apActive_) return;
  if (on) {
    WiFi.mode(configured() ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(apSsid_.c_str(), cfg_.apPassword.c_str());
    dns_.start(53, "*", WiFi.softAPIP());  // captive portal: every name resolves to us
  } else {
    dns_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
  }
  apActive_ = on;
}

void WifiService::loop(uint32_t now) {
  if (setupRequested_.exchange(false)) forcedApUntilMs_ = now + config::kSetupApMs;
  if (reconnectRequested_.exchange(false) && configured()) {
    if (!apActive_) WiFi.mode(WIFI_STA);
    connect();
  }

  const bool isConnected = configured() && WiFi.status() == WL_CONNECTED;
  if (isConnected && !connected_) {
    connectedSinceMs_ = now;
    if (!mdnsStarted_ && MDNS.begin(config::kHostname)) {
      mdnsStarted_ = true;
      MDNS.addService("http", "tcp", 80);
    }
    clock_->startNtp();
    lastInternetCheckMs_ = now - config::kInternetCheckMs;  // check right away
  }
  if (!isConnected && connected_) disconnectedSinceMs_ = now;
  connected_ = isConnected;

  if (!isConnected && configured() && now - lastAttemptMs_ > config::kStaRetryMs) connect();

  // Access point policy.
  const bool forced = static_cast<int32_t>(forcedApUntilMs_ - now) > 0;
  const bool fallback = configured() && !isConnected && now - disconnectedSinceMs_ > config::kFallbackApAfterMs;
  const bool lingering = apActive_ && isConnected && now - connectedSinceMs_ < config::kApOffAfterStaMs;
  setAp(!configured() || forced || fallback || lingering);
  if (apActive_) dns_.processNextRequest();

  if (isConnected && now - lastInternetCheckMs_ >= config::kInternetCheckMs) {
    lastInternetCheckMs_ = now;
    IPAddress addr;
    internet_ = WiFi.hostByName("pool.ntp.org", addr) == 1;
  } else if (!isConnected) {
    internet_ = false;
  }

  if (scanRequested_.exchange(false) && !scanning_) {
    WiFi.scanNetworks(true);
    scanning_ = true;
  }
  if (scanning_) {
    const int n = WiFi.scanComplete();
    if (n >= 0 || n == WIFI_SCAN_FAILED) {
      std::vector<Network> found;
      for (int i = 0; i < n; ++i) {
        std::string s = WiFi.SSID(i).c_str();
        if (s.empty()) continue;
        auto it = std::find_if(found.begin(), found.end(), [&](const Network& x) { return x.ssid == s; });
        if (it == found.end()) {
          found.push_back({s, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
        } else {
          it->rssi = std::max(it->rssi, static_cast<int>(WiFi.RSSI(i)));
        }
      }
      std::sort(found.begin(), found.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
      WiFi.scanDelete();
      xSemaphoreTake(scanMutex_, portMAX_DELAY);
      networks_ = found;
      xSemaphoreGive(scanMutex_);
      scanning_ = false;
    }
  }
}

bool WifiService::scanResults(JsonArray out) {
  xSemaphoreTake(scanMutex_, portMAX_DELAY);
  for (const auto& n : networks_) {
    JsonObject o = out.add<JsonObject>();
    o["ssid"] = n.ssid;
    o["rssi"] = n.rssi;
    o["secure"] = n.secure;
  }
  xSemaphoreGive(scanMutex_);
  return !scanning_;
}

std::string WifiService::ip() const {
  if (connected_) return WiFi.localIP().toString().c_str();
  if (apActive_) return WiFi.softAPIP().toString().c_str();
  return "";
}

int WifiService::rssi() const { return connected_ ? WiFi.RSSI() : 0; }

const char* WifiService::mode() const {
  if (connected_ && apActive_) return "sta+ap";
  if (connected_) return "sta";
  if (apActive_) return "ap";
  return "off";
}
