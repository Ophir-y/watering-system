#include <unity.h>

#include "Planner.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

static PlantProfile plant() {
  PlantProfile p;
  p.potVolumeMl = 2000;  // initial pulse 100 ml
  p.moisture = {20, 35, 55, 75};
  p.maxPulseMl = 120;
  p.maxDailyMl = 600;
  p.minIntervalH = 6;
  p.soakMin = 10;
  return p;
}

static PlannerInput input(float pct, MonoMs now = 0) {
  PlannerInput in;
  in.monoMs = now;
  in.sensorUsable = true;
  in.moisturePct = pct;
  return in;
}

static const MonoMs kMin = 60000;

void test_no_session_above_trigger() {
  Planner pl;
  PlantRuntime rt;
  pl.tick(plant(), rt, input(40));
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
}

void test_starts_session_below_trigger() {
  Planner pl;
  PlantRuntime rt;
  pl.tick(plant(), rt, input(30));
  TEST_ASSERT_EQUAL(PlannerState::WantPulse, pl.state());
  TEST_ASSERT_EQUAL(SessionKind::Auto, pl.kind());
  TEST_ASSERT_EQUAL_FLOAT(100, pl.pendingMl());
  TEST_ASSERT_FALSE(pl.continuation());
}

void test_respects_window_unless_critical() {
  Planner pl;
  PlantRuntime rt;
  PlannerInput in = input(30);
  in.inWindow = false;
  pl.tick(plant(), rt, in);
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
  in.moisturePct = 15;  // below critical
  pl.tick(plant(), rt, in);
  TEST_ASSERT_EQUAL(PlannerState::WantPulse, pl.state());
}

void test_respects_min_interval_and_daily_budget() {
  Planner pl;
  PlantRuntime rt;
  PlannerInput in = input(30);
  in.secondsSinceLastSession = 3600;
  pl.tick(plant(), rt, in);
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
  in.secondsSinceLastSession = 7 * 3600;
  rt.mlToday = 598;
  pl.tick(plant(), rt, in);
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
}

void test_pulse_and_soak_until_target() {
  Planner pl;
  PlantRuntime rt;
  pl.tick(plant(), rt, input(30, 0));
  pl.onDoseStarted();
  TEST_ASSERT_EQUAL(PlannerState::Pulsing, pl.state());
  pl.onDoseFinished(100, kMin, 10);
  TEST_ASSERT_EQUAL(PlannerState::Soaking, pl.state());

  pl.tick(plant(), rt, input(35, 5 * kMin));  // still soaking
  TEST_ASSERT_EQUAL(PlannerState::Soaking, pl.state());

  pl.tick(plant(), rt, input(40, 12 * kMin));  // +10 % for 100 ml -> learns 10 ml/%
  TEST_ASSERT_EQUAL(PlannerState::WantPulse, pl.state());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, rt.mlPerPct);
  TEST_ASSERT_TRUE(pl.continuation());
  // deficit 15 % * 10 ml/% * 0.7
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 105.0f, pl.pendingMl());

  pl.onDoseStarted();
  pl.onDoseFinished(105, 13 * kMin, 10);
  pl.tick(plant(), rt, input(54.8f, 24 * kMin));
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());

  SessionReport r;
  TEST_ASSERT_TRUE(pl.takeReport(r));
  TEST_ASSERT_EQUAL(SessionEnd::TargetReached, r.end);
  TEST_ASSERT_EQUAL(2, r.pulses);
  TEST_ASSERT_EQUAL_FLOAT(205, r.ml);
  TEST_ASSERT_FALSE(pl.takeReport(r));
}

void test_no_response_after_two_pulses() {
  Planner pl;
  PlantRuntime rt;
  pl.tick(plant(), rt, input(30, 0));
  pl.onDoseStarted();
  pl.onDoseFinished(100, kMin, 10);
  pl.tick(plant(), rt, input(30.3f, 12 * kMin));
  TEST_ASSERT_EQUAL(PlannerState::WantPulse, pl.state());
  pl.onDoseStarted();
  pl.onDoseFinished(100, 13 * kMin, 10);
  pl.tick(plant(), rt, input(30.5f, 24 * kMin));
  SessionReport r;
  TEST_ASSERT_TRUE(pl.takeReport(r));
  TEST_ASSERT_EQUAL(SessionEnd::NoResponse, r.end);
  // backoff: a new session does not start immediately
  pl.tick(plant(), rt, input(30.5f, 25 * kMin));
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
}

void test_limit_after_max_pulses() {
  Planner pl;
  PlantRuntime rt;
  PlantProfile p = plant();
  p.maxDailyMl = 5000;
  pl.tick(p, rt, input(10, 0));
  float pct = 10;
  MonoMs t = 0;
  for (int i = 0; i < kMaxPulsesPerSession; ++i) {
    TEST_ASSERT_EQUAL(PlannerState::WantPulse, pl.state());
    pl.onDoseStarted();
    pl.onDoseFinished(20, t + kMin, 2);
    pct += 2;  // slow response, but a response
    t += 5 * kMin;
    pl.tick(p, rt, input(pct, t));
  }
  SessionReport r;
  TEST_ASSERT_TRUE(pl.takeReport(r));
  TEST_ASSERT_EQUAL(SessionEnd::LimitReached, r.end);
  TEST_ASSERT_EQUAL(kMaxPulsesPerSession, r.pulses);
}

void test_soak_and_dry_waters_to_near_max() {
  PlantProfile p = plant();
  p.style = WateringStyle::SoakAndDry;
  TEST_ASSERT_EQUAL_FLOAT(72, sessionTargetPct(p));
}

void test_trigger_stays_below_target() {
  PlantProfile p = plant();
  WateringAdjust adj;
  adj.triggerOffsetPct = 30;
  TEST_ASSERT_EQUAL_FLOAT(52, triggerPct(p, adj));
  adj.triggerOffsetPct = -30;
  TEST_ASSERT_EQUAL_FLOAT(20, triggerPct(p, adj));  // never below critical
}

void test_dose_factor_scales_pulse() {
  Planner pl;
  PlantRuntime rt;
  PlannerInput in = input(30);
  in.adjust.doseFactor = 0.5f;
  pl.tick(plant(), rt, in);
  TEST_ASSERT_EQUAL_FLOAT(50, pl.pendingMl());
}

void test_fallback_schedule_when_sensor_unusable() {
  Planner pl;
  PlantRuntime rt;
  PlantProfile p = plant();
  p.onSensorFault = SensorFaultPolicy::FallbackSchedule;
  p.fallbackEveryDays = 3;
  p.fallbackMl = 150;
  PlannerInput in;
  in.sensorUsable = false;
  in.secondsSinceLastFallback = 2 * 86400;
  pl.tick(p, rt, in);
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
  in.secondsSinceLastFallback = 3 * 86400;
  in.fallbackAllowed = false;
  pl.tick(p, rt, in);
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
  in.fallbackAllowed = true;
  pl.tick(p, rt, in);
  TEST_ASSERT_EQUAL(PlannerState::WantPulse, pl.state());
  TEST_ASSERT_EQUAL(SessionKind::Fallback, pl.kind());
  TEST_ASSERT_EQUAL_FLOAT(150, pl.pendingMl());
  pl.onDoseStarted();
  pl.onDoseFinished(150, kMin, 10);
  SessionReport r;
  TEST_ASSERT_TRUE(pl.takeReport(r));
  TEST_ASSERT_EQUAL(SessionEnd::Single, r.end);
}

void test_pause_policy_does_nothing_without_sensor() {
  Planner pl;
  PlantRuntime rt;
  PlannerInput in;
  in.sensorUsable = false;
  pl.tick(plant(), rt, in);
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
}

void test_blocked_ends_session_with_backoff() {
  Planner pl;
  PlantRuntime rt;
  pl.tick(plant(), rt, input(30, 0));
  pl.onBlocked(0);
  SessionReport r;
  TEST_ASSERT_TRUE(pl.takeReport(r));
  TEST_ASSERT_EQUAL(SessionEnd::Blocked, r.end);
  pl.tick(plant(), rt, input(30, 10 * kMin));
  TEST_ASSERT_EQUAL(PlannerState::Idle, pl.state());
  pl.tick(plant(), rt, input(30, 31 * kMin));
  TEST_ASSERT_EQUAL(PlannerState::WantPulse, pl.state());
}

void test_sensor_loss_during_soak_aborts() {
  Planner pl;
  PlantRuntime rt;
  pl.tick(plant(), rt, input(30, 0));
  pl.onDoseStarted();
  pl.onDoseFinished(100, kMin, 10);
  PlannerInput in = input(30, 12 * kMin);
  in.sensorUsable = false;
  pl.tick(plant(), rt, in);
  SessionReport r;
  TEST_ASSERT_TRUE(pl.takeReport(r));
  TEST_ASSERT_EQUAL(SessionEnd::Aborted, r.end);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_session_above_trigger);
  RUN_TEST(test_starts_session_below_trigger);
  RUN_TEST(test_respects_window_unless_critical);
  RUN_TEST(test_respects_min_interval_and_daily_budget);
  RUN_TEST(test_pulse_and_soak_until_target);
  RUN_TEST(test_no_response_after_two_pulses);
  RUN_TEST(test_limit_after_max_pulses);
  RUN_TEST(test_soak_and_dry_waters_to_near_max);
  RUN_TEST(test_trigger_stays_below_target);
  RUN_TEST(test_dose_factor_scales_pulse);
  RUN_TEST(test_fallback_schedule_when_sensor_unusable);
  RUN_TEST(test_pause_policy_does_nothing_without_sensor);
  RUN_TEST(test_blocked_ends_session_with_backoff);
  RUN_TEST(test_sensor_loss_during_soak_aborts);
  return UNITY_END();
}
