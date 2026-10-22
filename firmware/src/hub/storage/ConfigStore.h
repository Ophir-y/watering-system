// Persistent configuration in NVS (survives power loss and firmware updates).
#pragma once

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>
#include <vector>

#include "Fleet.h"
#include "Types.h"

class ConfigStore {
 public:
  struct WifiConfig {
    std::string ssid;
    std::string password;
    std::string apPassword;
  };

  bool begin();

  bool loadSettings(plantcore::Settings& s);
  bool saveSettings(const plantcore::Settings& s);
  bool loadPlants(std::vector<plantcore::PlantProfile>& plants);
  bool savePlants(const std::vector<plantcore::PlantProfile>& plants);
  // Paired plant nodes and their keys.
  bool loadNodes(std::vector<plantlink::NodeRecord>& nodes);
  bool saveNodes(const std::vector<plantlink::NodeRecord>& nodes);
  bool loadStopped();
  bool saveStopped(bool stopped);

  WifiConfig loadWifi();
  bool saveWifi(const WifiConfig& w);

  bool hasPassword();
  bool checkPassword(const std::string& password);
  bool setPassword(const std::string& password);
  // SHA-256 hashes of issued API tokens (the tokens themselves are never stored).
  std::vector<std::string> loadTokenHashes();
  bool saveTokenHashes(const std::vector<std::string>& hashes);

  void factoryReset();

 private:
  bool putText(const char* key, const std::string& text);
  bool getText(const char* key, std::string& text);

  Preferences prefs_;
  SemaphoreHandle_t mutex_ = nullptr;
};
