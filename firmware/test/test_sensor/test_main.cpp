#include <unity.h>

#include "SensorMonitor.h"
#include "Signal.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

static const SensorCalibration kCal{21000, 10000};

static MoistureSample sample(int32_t raw, bool ok = true) { return MoistureSample{true, ok, raw}; }

void test_healthy_sensor() {
  SensorMonitor m;
  TEST_ASSERT_TRUE(m.onSample(sample(15000), kCal, 0, false));
  TEST_ASSERT_EQUAL(SensorHealth::Ok, m.health());
  TEST_ASSERT_EQUAL(SensorIssue::None, m.issue());
}

void test_uncalibrated() {
  SensorMonitor m;
  TEST_ASSERT_FALSE(m.onSample(sample(15000), SensorCalibration{15000, 15000}, 0, false));
  TEST_ASSERT_EQUAL(SensorIssue::Uncalibrated, m.issue());
  TEST_ASSERT_EQUAL(SensorHealth::Failed, m.health());
}

void test_bus_errors_trip_after_three() {
  SensorMonitor m;
  m.onSample(sample(0, false), kCal, 0, false);
  m.onSample(sample(0, false), kCal, 1, false);
  TEST_ASSERT_EQUAL(SensorHealth::Ok, m.health());
  m.onSample(sample(0, false), kCal, 2, false);
  TEST_ASSERT_EQUAL(SensorIssue::BusError, m.issue());
  m.onSample(sample(15000), kCal, 3, false);
  TEST_ASSERT_EQUAL(SensorHealth::Ok, m.health());
}

void test_disconnected_low_and_shorted_high() {
  SensorMonitor m;
  for (int i = 0; i < 3; ++i) m.onSample(sample(100), kCal, i, false);
  TEST_ASSERT_EQUAL(SensorIssue::Disconnected, m.issue());
  SensorMonitor h;
  for (int i = 0; i < 3; ++i) h.onSample(sample(32000), kCal, i, false);
  TEST_ASSERT_EQUAL(SensorIssue::Disconnected, h.issue());
}

void test_out_of_calibrated_range() {
  SensorMonitor m;
  // span 11000, 25 % margin -> valid 7250..23750
  TEST_ASSERT_TRUE(m.onSample(sample(23000), kCal, 0, false));
  for (int i = 0; i < 3; ++i) m.onSample(sample(25000), kCal, i, false);
  TEST_ASSERT_EQUAL(SensorIssue::OutOfRange, m.issue());
}

void test_stuck_reading_is_suspect_and_recovers() {
  SensorMonitor m;
  const MonoMs hour = 3600LL * 1000;
  for (int i = 0; i <= 13; ++i) m.onSample(sample(15000 + (i % 2)), kCal, i * hour, false);
  TEST_ASSERT_EQUAL(SensorIssue::Stuck, m.issue());
  TEST_ASSERT_EQUAL(SensorHealth::Suspect, m.health());
  m.onSample(sample(15200), kCal, 14 * hour, false);
  TEST_ASSERT_EQUAL(SensorHealth::Ok, m.health());
}

void test_noisy_but_alive_sensor_is_not_stuck() {
  SensorMonitor m;
  const MonoMs hour = 3600LL * 1000;
  for (int i = 0; i <= 25; ++i) m.onSample(sample(15000 + (i % 5) * 4), kCal, i * hour, false);
  TEST_ASSERT_EQUAL(SensorHealth::Ok, m.health());
}

void test_erratic_jumps() {
  SensorMonitor m;
  const MonoMs minute = 60000;
  int32_t a = 18000, b = 12000;  // ~27 % vs ~82 %
  for (int i = 0; i < 5; ++i) m.onSample(sample(i % 2 ? a : b), kCal, i * minute, false);
  TEST_ASSERT_EQUAL(SensorIssue::Erratic, m.issue());
  TEST_ASSERT_EQUAL(SensorHealth::Suspect, m.health());
}

void test_jumps_after_watering_are_expected() {
  SensorMonitor m;
  const MonoMs minute = 60000;
  for (int i = 0; i < 5; ++i) m.onSample(sample(i % 2 ? 18000 : 12000), kCal, i * minute, true);
  TEST_ASSERT_EQUAL(SensorHealth::Ok, m.health());
}

void test_no_response_clears_on_natural_rise() {
  SensorMonitor m;
  m.onSample(sample(18000), kCal, 0, false);  // ~27 %
  m.reportNoResponse(m.lastPct());
  TEST_ASSERT_EQUAL(SensorIssue::NoResponse, m.issue());
  TEST_ASSERT_EQUAL(SensorHealth::Failed, m.health());
  m.onSample(sample(17800), kCal, 60000, false);  // +2 %: not enough
  TEST_ASSERT_EQUAL(SensorIssue::NoResponse, m.issue());
  m.onSample(sample(16500), kCal, 120000, false);  // +14 %
  TEST_ASSERT_EQUAL(SensorHealth::Ok, m.health());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_healthy_sensor);
  RUN_TEST(test_uncalibrated);
  RUN_TEST(test_bus_errors_trip_after_three);
  RUN_TEST(test_disconnected_low_and_shorted_high);
  RUN_TEST(test_out_of_calibrated_range);
  RUN_TEST(test_stuck_reading_is_suspect_and_recovers);
  RUN_TEST(test_noisy_but_alive_sensor_is_not_stuck);
  RUN_TEST(test_erratic_jumps);
  RUN_TEST(test_jumps_after_watering_are_expected);
  RUN_TEST(test_no_response_clears_on_natural_rise);
  return UNITY_END();
}
