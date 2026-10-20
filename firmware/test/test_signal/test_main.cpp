#include <unity.h>

#include "EnvModel.h"
#include "Signal.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

static SensorCalibration cal() { return SensorCalibration{20000, 10000}; }

void test_raw_to_percent_endpoints() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, rawToPercent(20000, cal()));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, rawToPercent(10000, cal()));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, rawToPercent(15000, cal()));
}

void test_raw_to_percent_clamps() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, rawToPercent(25000, cal()));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, rawToPercent(5000, cal()));
}

void test_raw_to_percent_reversed_sensor() {
  SensorCalibration rev{10000, 20000};  // sensor that reads higher when wet
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, rawToPercent(12500, rev));
}

void test_invalid_calibration() {
  SensorCalibration bad{15000, 14500};
  TEST_ASSERT_FALSE(calibrationValid(bad));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rawToPercent(14000, bad));
  TEST_ASSERT_TRUE(calibrationValid(cal()));
}

void test_median() {
  int32_t v[] = {9, 1, 5, 1000, 3, 7, 2, 8, 4};
  TEST_ASSERT_EQUAL_INT32(5, medianInPlace(v, 9));
  int32_t one[] = {42};
  TEST_ASSERT_EQUAL_INT32(42, medianInPlace(one, 1));
}

void test_ema() {
  Ema e(0.5f);
  TEST_ASSERT_FALSE(e.primed());
  TEST_ASSERT_EQUAL_FLOAT(10.0f, e.update(10));
  TEST_ASSERT_EQUAL_FLOAT(15.0f, e.update(20));
  e.reset();
  TEST_ASSERT_EQUAL_FLOAT(4.0f, e.update(4));
}

void test_vpd_reference_values() {
  // 25 °C: saturation pressure ~3.17 kPa; at 50 % RH the deficit is half of that.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 3.17f, saturationVaporPressureKPa(25));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.58f, vpdKPa(25, 50));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vpdKPa(25, 100));
}

static PlantProfile profile() {
  PlantProfile p;
  p.tempC = {16, 30};
  p.lightLux = {1000, 10000};
  p.dormantMonths = (1u << 11) | (1u << 0) | (1u << 1);  // Dec, Jan, Feb
  p.dormancyFactor = 0.6f;
  return p;
}

void test_adjust_neutral_conditions() {
  EnvSnapshot env{true, 22, 63, false, 0};  // VPD ~0.98 kPa
  WateringAdjust a = computeAdjust(profile(), env, 6, false);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 0.0f, a.triggerOffsetPct);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, a.doseFactor);
  TEST_ASSERT_FALSE(a.dormant);
}

void test_adjust_hot_dry_waters_sooner() {
  EnvSnapshot env{true, 32, 20, true, 50000};
  WateringAdjust a = computeAdjust(profile(), env, 6, false);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, a.triggerOffsetPct);  // capped
}

void test_adjust_cold_halves_dose() {
  EnvSnapshot env{true, 12, 60, false, 0};
  WateringAdjust a = computeAdjust(profile(), env, 6, false);
  TEST_ASSERT_TRUE(a.tooCold);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, a.doseFactor);
  TEST_ASSERT_TRUE(a.triggerOffsetPct <= 0);
}

void test_adjust_dormancy_and_hemisphere() {
  EnvSnapshot none;
  WateringAdjust jan = computeAdjust(profile(), none, 1, false);
  TEST_ASSERT_TRUE(jan.dormant);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, jan.doseFactor);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -4.0f, jan.triggerOffsetPct);
  // Southern hemisphere: northern winter months map to July/August.
  TEST_ASSERT_FALSE(computeAdjust(profile(), none, 1, true).dormant);
  TEST_ASSERT_TRUE(computeAdjust(profile(), none, 7, true).dormant);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_raw_to_percent_endpoints);
  RUN_TEST(test_raw_to_percent_clamps);
  RUN_TEST(test_raw_to_percent_reversed_sensor);
  RUN_TEST(test_invalid_calibration);
  RUN_TEST(test_median);
  RUN_TEST(test_ema);
  RUN_TEST(test_vpd_reference_values);
  RUN_TEST(test_adjust_neutral_conditions);
  RUN_TEST(test_adjust_hot_dry_waters_sooner);
  RUN_TEST(test_adjust_cold_halves_dose);
  RUN_TEST(test_adjust_dormancy_and_hemisphere);
  return UNITY_END();
}
