// Persistent configuration in NVS (survives power loss and firmware updates).
#pragma once

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>
#include <vector>

#include "Types.h"

class ConfigStore {
 public:
  struct WifiConfig {
    std::string ssid;
    std::string password;
    std::string apPassword;
  };
  struct ScaleCal {
    int32_t offset = 0;
    float countsPerGram = 0;
  };

  bool begin();

  bool loadSettings(plantcore::Settings& s);
  bool saveSettings(const plantcore::Settings& s);
  bool loadPlants(std::vector<plantcore::PlantProfile>& plants);
  bool savePlants(const std::vector<plantcore::PlantProfile>& plants);
  bool loadState(plantcore::PersistedState& s);
  bool saveState(const plantcore::PersistedState& s);

  WifiConfig loadWifi();
  bool saveWifi(const WifiConfig& w);
  ScaleCal loadScale();
  bool saveScale(const ScaleCal& c);

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
