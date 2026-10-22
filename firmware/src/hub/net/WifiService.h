// Wi-Fi: joins the home network, and runs its own access point when there is no
// configured network, when the home network has been unreachable for a while, or
// on request (long press of the button). The device keeps watering either way.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>

#include <atomic>
#include <string>
#include <vector>

#include "../storage/ConfigStore.h"

class TimeKeeper;

class WifiService {
 public:
  void begin(ConfigStore& store, TimeKeeper& clock);
  void loop(uint32_t nowMs);  // network task

  void requestSetupAp() { setupRequested_ = true; }
  void setCredentials(const std::string& ssid, const std::string& password, const std::string& apPassword);
  void requestScan() { scanRequested_ = true; }
  bool scanResults(JsonArray out);  // false while a scan is running

  bool configured() const { return !cfg_.ssid.empty(); }
  bool staConnected() const { return connected_; }
  bool apActive() const { return apActive_; }
  bool internet() const { return internet_; }
  std::string ssid() const { return cfg_.ssid; }
  std::string apSsid() const { return apSsid_; }
  std::string ip() const;
  int rssi() const;
  const char* mode() const;

 private:
  void setAp(bool on);
  void connect();

  ConfigStore* store_ = nullptr;
  TimeKeeper* clock_ = nullptr;
  ConfigStore::WifiConfig cfg_;
  std::string apSsid_;
  DNSServer dns_;

  std::atomic<bool> connected_{false};
  std::atomic<bool> apActive_{false};
  std::atomic<bool> internet_{false};
  std::atomic<bool> setupRequested_{false};
  std::atomic<bool> reconnectRequested_{false};
  std::atomic<bool> scanRequested_{false};
  std::atomic<bool> scanning_{false};

  uint32_t lastAttemptMs_ = 0;
  uint32_t disconnectedSinceMs_ = 0;
  uint32_t connectedSinceMs_ = 0;
  uint32_t forcedApUntilMs_ = 0;
  uint32_t lastInternetCheckMs_ = 0;
  bool mdnsStarted_ = false;

  struct Network {
    std::string ssid;
    int rssi;
    bool secure;
  };
  std::vector<Network> networks_;
  SemaphoreHandle_t scanMutex_ = nullptr;
};
