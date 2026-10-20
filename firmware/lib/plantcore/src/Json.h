// JSON (de)serialisation for the REST API and NVS storage. The shapes produced
// here are the API contract mirrored by dashboard/src/api/types.ts and checked
// against shared/api-fixtures/ by the native tests.
#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "Engine.h"
#include "Types.h"

namespace plantcore {

const char* outletTypeName(OutletType t);
const char* wateringStyleName(WateringStyle s);
const char* faultPolicyName(SensorFaultPolicy p);
const char* sensorHealthName(SensorHealth h);
const char* eventTypeName(EventType t);
bool parseEventType(const std::string& s, EventType& out);

// Device/network facts the engine does not know about, filled in by the firmware.
struct SystemInfo {
  std::string version;
  int64_t uptimeS = 0;
  bool timeValid = false;
  EpochS now = 0;
  uint32_t heapFree = 0;
  std::string resetReason;
  std::string wifiMode;  // "sta", "ap", "sta+ap", "off"
  std::string ssid;
  std::string ip;
  int rssi = 0;
  bool apActive = false;
  std::string apSsid;
  bool internet = false;
  bool sdOk = false;
  float sdUsedMb = 0;
  float sdTotalMb = 0;
  bool scaleCalibrated = false;
  int32_t scaleRaw = 0;
};

// Plants. fromJson applies only the keys present (partial update), then validates.
void toJson(const PlantProfile& p, JsonObject out);
bool fromJson(JsonObjectConst in, PlantProfile& p, std::string& err);
bool validateProfile(const PlantProfile& p, std::string& err);

// Settings. The ntfy token is never echoed back, only whether one is set.
void toJson(const Settings& s, JsonObject out);
bool fromJson(JsonObjectConst in, Settings& s, std::string& err);
bool validateSettings(const Settings& s, std::string& err);

void toJson(const PersistedState& s, JsonObject out);
bool fromJson(JsonObjectConst in, PersistedState& s);

// plantId: id of the plant on the alert's/event's channel ("" if none).
void toJson(const Alert& a, const std::string& plantId, JsonObject out);
void toJson(const Event& e, const std::string& plantId, JsonObject out);

void statusToJson(const EngineStatus& st, const SystemInfo& sys, const Settings& settings, JsonObject out);

}  // namespace plantcore
