#include "History.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Json.h"

namespace plantcore {

namespace {

// Splits on commas; a field starting with '"' runs to its closing quote ("" escapes a quote).
std::vector<std::string> splitCsv(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  size_t i = 0;
  const size_t n = line.size();
  while (i <= n) {
    cur.clear();
    if (i < n && line[i] == '"') {
      ++i;
      while (i < n) {
        if (line[i] == '"') {
          if (i + 1 < n && line[i + 1] == '"') {
            cur += '"';
            i += 2;
            continue;
          }
          ++i;
          break;
        }
        cur += line[i++];
      }
      while (i < n && line[i] != ',') ++i;
    } else {
      while (i < n && line[i] != ',' && line[i] != '\r' && line[i] != '\n') cur += line[i++];
      while (i < n && line[i] != ',') ++i;
    }
    out.push_back(cur);
    ++i;  // skip the comma (or step past the end)
  }
  return out;
}

bool parseInt64(const std::string& s, int64_t& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  long long v = std::strtoll(s.c_str(), &end, 10);
  if (*end != '\0') return false;
  out = v;
  return true;
}

bool parseFloat(const std::string& s, float& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  float v = std::strtof(s.c_str(), &end);
  if (*end != '\0' || !std::isfinite(v)) return false;
  out = v;
  return true;
}

std::string quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') {
      out += "\"\"";
    } else if (c == '\n' || c == '\r') {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
}

std::string num1(float v) {
  char buf[24];
  snprintf(buf, sizeof buf, "%.1f", v);
  return buf;
}

}  // namespace

std::string formatReading(const ReadingRow& r) {
  char buf[96];
  snprintf(buf, sizeof buf, "%lld,%s,%ld,%.1f", static_cast<long long>(r.ts), r.plantId.c_str(),
           static_cast<long>(r.raw), r.pct);
  return buf;
}

bool parseReading(const std::string& line, ReadingRow& r) {
  auto f = splitCsv(line);
  if (f.size() != 4) return false;
  int64_t ts = 0, raw = 0;
  float pct = 0;
  if (!parseInt64(f[0], ts) || f[1].empty() || !parseInt64(f[2], raw) || !parseFloat(f[3], pct)) return false;
  r.ts = ts;
  r.plantId = f[1];
  r.raw = static_cast<int32_t>(raw);
  r.pct = pct;
  return true;
}

std::string formatEnv(const EnvRow& r) {
  std::string out = std::to_string(r.ts);
  for (int i = 0; i < kEnvColumns; ++i) {
    out += ',';
    if (r.valid[i]) out += num1(r.values[i]);
  }
  return out;
}

bool parseEnv(const std::string& line, EnvRow& r) {
  auto f = splitCsv(line);
  if (f.size() != 1 + kEnvColumns) return false;
  int64_t ts = 0;
  if (!parseInt64(f[0], ts)) return false;
  r.ts = ts;
  for (int i = 0; i < kEnvColumns; ++i) {
    r.valid[i] = parseFloat(f[1 + i], r.values[i]);
    if (!r.valid[i]) r.values[i] = 0;
  }
  return true;
}

std::string formatEvent(const Event& e, const std::string& plantId) {
  return std::to_string(e.ts) + ',' + eventTypeName(e.type) + ',' + plantId + ',' + e.code + ',' + num1(e.ml) + ',' +
         quote(e.detail);
}

bool parseEvent(const std::string& line, Event& e, std::string& plantId) {
  auto f = splitCsv(line);
  if (f.size() != 6) return false;
  int64_t ts = 0;
  float ml = 0;
  EventType type;
  if (!parseInt64(f[0], ts) || !parseEventType(f[1], type) || !parseFloat(f[4], ml)) return false;
  e.ts = ts;
  e.type = type;
  e.channel = -1;
  plantId = f[2];
  e.code = f[3];
  e.ml = ml;
  e.detail = f[5];
  return true;
}

// ------------------------------------------------------------------ downsampling

Downsampler::Downsampler(EpochS from, EpochS to, size_t buckets, size_t columns)
    : from_(from), to_(to > from ? to : from + 1), columns_(columns), buckets_(buckets ? buckets : 1),
      sums_(buckets_.size() * columns, 0.0), counts_(buckets_.size() * columns, 0) {}

void Downsampler::add(EpochS ts, const float* values, const bool* valid) {
  if (ts < from_ || ts >= to_) return;
  const size_t n = buckets_.size();
  size_t b = static_cast<size_t>((ts - from_) * static_cast<double>(n) / static_cast<double>(to_ - from_));
  if (b >= n) b = n - 1;
  bool any = false;
  for (size_t c = 0; c < columns_; ++c) {
    if (!valid[c]) continue;
    sums_[b * columns_ + c] += values[c];
    counts_[b * columns_ + c]++;
    any = true;
  }
  if (any) {
    buckets_[b].tsSum += static_cast<double>(ts);
    buckets_[b].n++;
  }
}

void Downsampler::toJson(JsonArray out) const {
  for (size_t b = 0; b < buckets_.size(); ++b) {
    if (buckets_[b].n == 0) continue;
    JsonArray row = out.add<JsonArray>();
    row.add(static_cast<int64_t>(std::llround(buckets_[b].tsSum / buckets_[b].n)));
    for (size_t c = 0; c < columns_; ++c) {
      uint32_t k = counts_[b * columns_ + c];
      if (k == 0) {
        row.add(nullptr);
      } else {
        row.add(std::round(sums_[b * columns_ + c] / k * 10.0) / 10.0);
      }
    }
  }
}

size_t Downsampler::nonEmpty() const {
  size_t n = 0;
  for (const auto& b : buckets_) n += b.n > 0;
  return n;
}

void seriesToJson(const Downsampler& d, EpochS from, EpochS to, JsonObject out) {
  out["from"] = from;
  out["to"] = to;
  d.toJson(out["points"].to<JsonArray>());
}

}  // namespace plantcore
