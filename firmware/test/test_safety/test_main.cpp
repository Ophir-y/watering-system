#include <unity.h>

#include "Safety.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

static SafetyLimits limits() { return SafetyLimits{}; }

static PlantProfile plant() {
  PlantProfile p;
  p.maxPulseMl = 100;
  p.maxDailyMl = 300;
  p.minIntervalH = 6;
  return p;
}

static DispenseRequest autoReq(float ml, float sinceS = 1e9f) {
  DispenseRequest r;
  r.kind = SessionKind::Auto;
  r.ml = ml;
  r.secondsSinceLastSession = sinceS;
  return r;
}

static Verdict eval(const NodeState& g, const PlantRuntime& rt, const DispenseRequest& r,
                    SensorHealth h = SensorHealth::Ok, PlantProfile p = plant()) {
  return evaluateDispense(limits(), g, p, rt, h, r);
}

void test_allows_normal_dose() {
  Verdict v = eval({}, {}, autoReq(50));
  TEST_ASSERT_TRUE(v.allowed);
  TEST_ASSERT_EQUAL_FLOAT(50, v.ml);
}

void test_interlocks_block_everything_including_manual() {
  DispenseRequest manual;
  manual.kind = SessionKind::Manual;
  manual.ml = 50;
  NodeState estop;
  estop.emergencyStop = true;
  NodeState empty;
  empty.bottleEmpty = true;
  NodeState flat;
  flat.batteryCritical = true;
  TEST_ASSERT_EQUAL(BlockReason::EmergencyStop, eval(estop, {}, manual).reason);
  TEST_ASSERT_EQUAL(BlockReason::BottleEmpty, eval(empty, {}, manual).reason);
  TEST_ASSERT_EQUAL(BlockReason::BatteryLow, eval(flat, {}, manual).reason);
  PlantRuntime locked;
  locked.lockedOut = true;
  TEST_ASSERT_EQUAL(BlockReason::Lockout, eval({}, locked, manual).reason);
}

void test_automation_rules_skip_for_manual() {
  NodeState paused;
  paused.automationEnabled = false;
  TEST_ASSERT_EQUAL(BlockReason::AutomationPaused, eval(paused, {}, autoReq(50)).reason);
  TEST_ASSERT_EQUAL(BlockReason::SensorFault, eval({}, {}, autoReq(50), SensorHealth::Failed).reason);
  TEST_ASSERT_EQUAL(BlockReason::MinInterval, eval({}, {}, autoReq(50, 3600)).reason);

  DispenseRequest manual;
  manual.kind = SessionKind::Manual;
  manual.ml = 50;
  manual.secondsSinceLastSession = 10;
  TEST_ASSERT_TRUE(eval(paused, {}, manual, SensorHealth::Failed).allowed);
}

void test_session_continuation_ignores_interval() {
  DispenseRequest r = autoReq(50, 600);
  r.continuation = true;
  TEST_ASSERT_TRUE(eval({}, {}, r).allowed);
}

void test_fallback_allowed_with_failed_sensor() {
  DispenseRequest r = autoReq(50);
  r.kind = SessionKind::Fallback;
  TEST_ASSERT_TRUE(eval({}, {}, r, SensorHealth::Failed).allowed);
}

void test_manual_only_plant() {
  PlantProfile p = plant();
  p.autoWater = false;
  TEST_ASSERT_EQUAL(BlockReason::ManualOnly, eval({}, {}, autoReq(50), SensorHealth::Ok, p).reason);
  p.enabled = false;
  TEST_ASSERT_EQUAL(BlockReason::PlantDisabled, eval({}, {}, autoReq(50), SensorHealth::Ok, p).reason);
}

void test_pulse_is_clamped() {
  TEST_ASSERT_EQUAL_FLOAT(100, eval({}, {}, autoReq(500)).ml);  // plant pulse cap
  DispenseRequest manual;
  manual.kind = SessionKind::Manual;
  manual.ml = 500;
  TEST_ASSERT_EQUAL_FLOAT(250, eval({}, {}, manual).ml);  // hard pulse cap
}

void test_daily_limits() {
  PlantRuntime rt;
  rt.mlToday = 260;
  Verdict v = eval({}, rt, autoReq(100));
  TEST_ASSERT_TRUE(v.allowed);
  TEST_ASSERT_EQUAL_FLOAT(40, v.ml);
  rt.mlToday = 298;
  TEST_ASSERT_EQUAL(BlockReason::PlantDailyLimit, eval({}, rt, autoReq(100)).reason);
}

void test_invalid_amount() {
  TEST_ASSERT_EQUAL(BlockReason::InvalidAmount, eval({}, {}, autoReq(0)).reason);
  TEST_ASSERT_EQUAL(BlockReason::InvalidAmount, eval({}, {}, autoReq(-5)).reason);
}

void test_max_on_time() {
  SafetyLimits l;
  TEST_ASSERT_EQUAL_UINT32(78000, maxOnTimeMs(l, 100, 2));  // 50 s expected * 1.5 + 3 s
  TEST_ASSERT_EQUAL_UINT32(l.hardMaxOnTimeMs, maxOnTimeMs(l, 1000, 1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_allows_normal_dose);
  RUN_TEST(test_interlocks_block_everything_including_manual);
  RUN_TEST(test_automation_rules_skip_for_manual);
  RUN_TEST(test_session_continuation_ignores_interval);
  RUN_TEST(test_fallback_allowed_with_failed_sensor);
  RUN_TEST(test_manual_only_plant);
  RUN_TEST(test_pulse_is_clamped);
  RUN_TEST(test_daily_limits);
  RUN_TEST(test_invalid_amount);
  RUN_TEST(test_max_on_time);
  return UNITY_END();
}
