// CSV row formats for the SD-card logs, and downsampling for chart queries.
//
//   readings/YYYY-MM-DD.csv  ts,plant,raw,pct
//   env/YYYY-MM-DD.csv       ts,tempC,humidityPct,lux,reservoirMl   (empty = no value)
//   events/YYYY-MM.csv       ts,type,plant,code,ml,"detail"
#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "Types.h"

namespace plantcore {

constexpr const char* kReadingsHeader = "ts,plant,raw,pct";
constexpr const char* kEnvHeader = "ts,tempC,humidityPct,lux,reservoirMl";
constexpr const char* kEventsHeader = "ts,type,plant,code,ml,detail";

struct ReadingRow {
  EpochS ts = 0;
  std::string plantId;
  int32_t raw = 0;
  float pct = 0;
};

constexpr int kEnvColumns = 4;
struct EnvRow {
  EpochS ts = 0;
  float values[kEnvColumns] = {0, 0, 0, 0};  // tempC, humidityPct, lux, reservoirMl
  bool valid[kEnvColumns] = {false, false, false, false};
};

std::string formatReading(const ReadingRow& r);
bool parseReading(const std::string& line, ReadingRow& r);

std::string formatEnv(const EnvRow& r);
bool parseEnv(const std::string& line, EnvRow& r);

std::string formatEvent(const Event& e, const std::string& plantId);
bool parseEvent(const std::string& line, Event& e, std::string& plantId);

// Averages samples into fixed-width time buckets so a chart never receives more
// than `buckets` points regardless of the queried range.
class Downsampler {
 public:
  Downsampler(EpochS from, EpochS to, size_t buckets, size_t columns);
  void add(EpochS ts, const float* values, const bool* valid);
  // Appends [ts, v1, ..., vn] per non-empty bucket; ts is the bucket average time.
  void toJson(JsonArray out) const;
  size_t nonEmpty() const;

 private:
  struct Bucket {
    double tsSum = 0;
    uint32_t n = 0;
  };
  EpochS from_;
  EpochS to_;
  size_t columns_;
  std::vector<Bucket> buckets_;
  std::vector<double> sums_;      // buckets x columns
  std::vector<uint32_t> counts_;  // buckets x columns
};

// History API response body: {"from", "to", "points": [[ts, ...], ...]}. The caller
// adds "plantId" for moisture series.
void seriesToJson(const Downsampler& d, EpochS from, EpochS to, JsonObject out);

}  // namespace plantcore
