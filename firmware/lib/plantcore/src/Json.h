// JSON (de)serialisation for the REST API and NVS storage. The shapes produced
// here are the API contract mirrored by dashboard/src/api/types.ts and checked
// against shared/api-fixtures/ by the native tests.
#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "Types.h"

namespace plantcore {

const char* wateringStyleName(WateringStyle s);
const char* faultPolicyName(SensorFaultPolicy p);
const char* sensorHealthName(SensorHealth h);
const char* eventTypeName(EventType t);
bool parseEventType(const std::string& s, EventType& out);

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

// What the main station sends a node: the plant profile (without the long texts the
// node never needs) and the settings that matter for watering.
void nodeConfigToJson(const Settings& s, const PlantProfile& p, JsonObject out);
// Applies a node config on top of the given values (partial, then validated).
bool nodeConfigFromJson(JsonObjectConst in, Settings& s, PlantProfile& p, std::string& err);

}  // namespace plantcore
