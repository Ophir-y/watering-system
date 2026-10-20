#include <unity.h>

#include <cmath>

#include "History.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

void test_reading_round_trip() {
  ReadingRow r{1767225600, "monstera-1", 15234, 42.37f};
  std::string line = formatReading(r);
  TEST_ASSERT_EQUAL_STRING("1767225600,monstera-1,15234,42.4", line.c_str());
  ReadingRow back;
  TEST_ASSERT_TRUE(parseReading(line, back));
  TEST_ASSERT_EQUAL_INT64(r.ts, back.ts);
  TEST_ASSERT_EQUAL_STRING("monstera-1", back.plantId.c_str());
  TEST_ASSERT_EQUAL_INT32(15234, back.raw);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.4f, back.pct);
  TEST_ASSERT_TRUE(parseReading(line + "\r", back));  // CRLF tolerant
}

void test_reading_rejects_garbage() {
  ReadingRow r;
  TEST_ASSERT_FALSE(parseReading(kReadingsHeader, r));
  TEST_ASSERT_FALSE(parseReading("", r));
  TEST_ASSERT_FALSE(parseReading("1,a,2", r));
  TEST_ASSERT_FALSE(parseReading("1,a,x,3", r));
}

void test_env_with_missing_values() {
  EnvRow r;
  r.ts = 100;
  r.values[0] = 21.5f;
  r.valid[0] = true;
  r.values[3] = 3200;
  r.valid[3] = true;
  std::string line = formatEnv(r);
  TEST_ASSERT_EQUAL_STRING("100,21.5,,,3200.0", line.c_str());
  EnvRow back;
  TEST_ASSERT_TRUE(parseEnv(line, back));
  TEST_ASSERT_TRUE(back.valid[0]);
  TEST_ASSERT_FALSE(back.valid[1]);
  TEST_ASSERT_FALSE(back.valid[2]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3200, back.values[3]);
}

void test_event_quoting() {
  Event e;
  e.ts = 5;
  e.type = EventType::Watering;
  e.code = "auto";
  e.ml = 120;
  e.detail = "2 pulses, 30% -> 55%, \"target\" reached\nok";
  std::string line = formatEvent(e, "fern");
  Event back;
  std::string plant;
  TEST_ASSERT_TRUE(parseEvent(line, back, plant));
  TEST_ASSERT_EQUAL_STRING("fern", plant.c_str());
  TEST_ASSERT_EQUAL(EventType::Watering, back.type);
  TEST_ASSERT_EQUAL_STRING("2 pulses, 30% -> 55%, \"target\" reached ok", back.detail.c_str());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 120, back.ml);
}

void test_event_without_plant() {
  Event e;
  e.ts = 9;
  e.type = EventType::Refill;
  e.code = "refill";
  e.ml = 1500;
  std::string line = formatEvent(e, "");
  Event back;
  std::string plant = "x";
  TEST_ASSERT_TRUE(parseEvent(line, back, plant));
  TEST_ASSERT_TRUE(plant.empty());
  TEST_ASSERT_EQUAL(EventType::Refill, back.type);
}

void test_downsampler_buckets_and_gaps() {
  Downsampler d(0, 1000, 10, 2);
  for (int t = 0; t < 1000; t += 10) {
    float v[2] = {static_cast<float>(t), 1.0f};
    bool ok[2] = {true, t < 500};
    d.add(t, v, ok);
  }
  float out[2] = {0, 0};
  bool ok[2] = {true, true};
  d.add(5000, out, ok);  // out of range: ignored
  TEST_ASSERT_EQUAL(10u, d.nonEmpty());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  d.toJson(arr);
  TEST_ASSERT_EQUAL(10u, arr.size());
  // first bucket: t = 0..90, average 45
  TEST_ASSERT_EQUAL_INT64(45, arr[0][0].as<int64_t>());
  TEST_ASSERT_FLOAT_WITHIN(0.01, 45.0, arr[0][1].as<double>());
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1.0, arr[0][2].as<double>());
  TEST_ASSERT_TRUE(arr[9][2].isNull());
}

void test_downsampler_skips_empty_buckets() {
  Downsampler d(0, 100, 50, 1);
  float v = 1;
  bool ok = true;
  d.add(10, &v, &ok);
  d.add(90, &v, &ok);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  d.toJson(arr);
  TEST_ASSERT_EQUAL(2u, arr.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reading_round_trip);
  RUN_TEST(test_reading_rejects_garbage);
  RUN_TEST(test_env_with_missing_values);
  RUN_TEST(test_event_quoting);
  RUN_TEST(test_event_without_plant);
  RUN_TEST(test_downsampler_buckets_and_gaps);
  RUN_TEST(test_downsampler_skips_empty_buckets);
  return UNITY_END();
}
