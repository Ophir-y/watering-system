// What a plant node keeps in flash (NVS): its pairing with the main station, the
// config it was sent, and the engine's state.
#pragma once

#include <Preferences.h>

#include <string>

#include "NodeSession.h"
#include "Types.h"

class NodeStore {
 public:
  void begin();

  bool loadPairing(plantlink::Pairing& p);
  void savePairing(const plantlink::Pairing& p);

  bool loadConfig(std::string& json);
  void saveConfig(const std::string& json);
  void clearConfig();

  bool loadState(plantcore::PersistedState& s);
  void saveState(const plantcore::PersistedState& s);

  void eraseAll();

 private:
  Preferences prefs_;
};
