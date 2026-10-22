#include <unity.h>

#include "../sim.h"

using namespace plantcore;
using namespace sim;

void setUp() {}
void tearDown() {}

static const MonoMs kMin = 60000;
static const MonoMs kHour = 60 * kMin;

struct Fixture {
  World world;
  RecordingSink sink;
  Engine engine{world, sink};
  Settings settings = world.settings();
  PlantProfile plant = makePlant("fern");

  Fixture() {
    world.plant.moisture = 50;
    engine.configure(settings, &plant);
    engine.restore(PersistedState{}, world.inputs());
    world.lastSampleMs = -1000000;  // let the first tick take a fresh sample
  }
  void reconfigure() { engine.configure(settings, &plant); }
  NodeStatus status() const { return engine.status(); }
  // Teach the pump monitor the wet current with a few flow-measuring runs.
  void learnPump() {
    for (int i = 0; i < kLearnRuns; ++i) {
      TEST_ASSERT_EQUAL_STRING("", engine.timedRun(3).c_str());
      run(world, engine, 30000);
    }
    TEST_ASSERT_TRUE(status().pumpReady);
  }
};

void test_waters_dry_plant_to_target() {
  Fixture f;
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 2 * kHour);
  TEST_ASSERT_TRUE(f.sink.wateredMl() > 100);
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 55.0f, f.world.plant.moisture);
  TEST_ASSERT_EQUAL(1, f.sink.count(EventType::Watering, "auto"));
  TEST_ASSERT_TRUE(f.status().mlPerPct > 0);  // learned
  TEST_ASSERT_TRUE(f.status().lastWateredAt > 0);
  // The gauge counted what left the bottle.
  TEST_ASSERT_FLOAT_WITHIN(3.0f, f.world.bottleMl, f.status().bottle.ml);
}

void test_persists_before_starting_pump() {
  struct CheckingSink : RecordingSink {
    World* world = nullptr;
    bool sawPumpWhilePersisting = false;
    void onPersist(const PersistedState& s) override {
      if (s.dispenseInProgress && world->pumpOn) sawPumpWhilePersisting = true;
      RecordingSink::onPersist(s);
    }
  };
  World w;
  CheckingSink sink;
  sink.world = &w;
  Engine e(w, sink);
  w.plant.moisture = 30;
  PlantProfile p = makePlant("fern");
  e.configure(w.settings(), &p);
  e.restore(PersistedState{}, w.inputs());
  run(w, e, 2 * kMin);
  TEST_ASSERT_TRUE(sink.persists > 0);
  TEST_ASSERT_TRUE(w.dispensedTotal > 0);
  TEST_ASSERT_FALSE(sink.sawPumpWhilePersisting);
}

void test_manual_watering() {
  Fixture f;
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater(80).c_str());
  TEST_ASSERT_EQUAL_STRING("busy", f.engine.manualWater(50).c_str());
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 80.0f, f.sink.wateredMl());
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 80.0f, f.world.dispensedTotal);
  TEST_ASSERT_EQUAL(1, f.sink.count(EventType::Watering, "manual"));
}

void test_timed_run_for_flow_calibration() {
  Fixture f;
  f.world.flowMlPerS = 3.0f;  // real flow differs from the profile's 2 ml/s
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_EQUAL_STRING("invalid_amount", f.engine.timedRun(0.5f).c_str());
  TEST_ASSERT_EQUAL_STRING("", f.engine.timedRun(10).c_str());
  run(f.world, f.engine, 1 * kMin);
  NodeStatus st = f.status();
  TEST_ASSERT_TRUE(st.lastDose.valid);
  TEST_ASSERT_TRUE(st.lastDose.timed);
  TEST_ASSERT_INT_WITHIN(150, 10000, static_cast<int>(st.lastDose.onTimeMs));
  // 10 s at the real 3 ml/s: the user measures ~30 ml in the cup and sets the flow from that.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 30.0f, f.world.dispensedTotal);
  TEST_ASSERT_EQUAL(1, f.sink.count(EventType::Watering, "timed"));
}

void test_station_stop_halts_pump_until_resumed() {
  Fixture f;
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 30000);  // first pulse running
  TEST_ASSERT_TRUE(f.world.pumpOn);
  f.engine.setStopped(true, "stopped from the dashboard");
  TEST_ASSERT_FALSE(f.world.pumpOn);
  TEST_ASSERT_TRUE(f.status().stopped);
  run(f.world, f.engine, 1 * kHour);
  TEST_ASSERT_FALSE(f.engine.doseActive());
  TEST_ASSERT_EQUAL_STRING("emergency_stop", f.engine.manualWater(50).c_str());
  TEST_ASSERT_TRUE(f.sink.last.stopped);  // survives a reboot
  f.engine.setStopped(false, "");
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater(50).c_str());
}

void test_gauge_counts_down_and_refill_resets() {
  Fixture f;
  f.engine.refilled(0);
  run(f.world, f.engine, 1 * kMin);
  f.engine.manualWater(100);
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 900.0f, f.status().bottle.ml);
  f.engine.refilled(0);
  TEST_ASSERT_EQUAL_FLOAT(1000, f.status().bottle.ml);
  TEST_ASSERT_EQUAL(2, f.sink.count(EventType::Refill, "refill"));
}

void test_gauge_empty_blocks_watering_without_current_sensing() {
  Fixture f;
  f.plant.pump.currentSense = false;
  f.plant.bottle.capacityMl = 300;
  f.reconfigure();
  f.engine.refilled(0);
  f.world.bottleMl = 300;
  f.world.currentSensor = false;
  f.plant.maxDailyMl = 2000;
  f.reconfigure();
  f.world.plant.dryPctPerHour = 3;
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 24 * kHour);
  TEST_ASSERT_TRUE(f.status().bottle.empty);
  TEST_ASSERT_EQUAL(EmptyReason::Gauge, f.status().bottle.reason);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::BottleEmpty, 0));
  TEST_ASSERT_TRUE(f.world.dispensedTotal <= 300 - 50 + 1);  // the reserve keeps the intake under water
  TEST_ASSERT_EQUAL_STRING("bottle_empty", f.engine.manualWater(50).c_str());

  f.world.bottleMl = 300;
  f.engine.refilled(0);
  TEST_ASSERT_FALSE(f.engine.alerts().isActive(AlertCode::BottleEmpty, 0));
  const float before = f.world.dispensedTotal;
  run(f.world, f.engine, 12 * kHour);
  TEST_ASSERT_TRUE(f.world.dispensedTotal > before);
}

void test_pump_running_dry_marks_bottle_empty() {
  Fixture f;
  f.learnPump();
  f.world.bottleMl = 100;  // much less than the gauge believes
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater(150).c_str());
  run(f.world, f.engine, 3 * kMin);
  const NodeStatus st = f.status();
  TEST_ASSERT_TRUE(st.bottle.empty);
  TEST_ASSERT_EQUAL(EmptyReason::PumpDry, st.bottle.reason);
  TEST_ASSERT_EQUAL(PumpVerdict::Dry, st.pumpVerdict);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::BottleEmpty, 0));
  TEST_ASSERT_FALSE(f.world.pumpOn);
  // It stopped within a couple of seconds of sucking air: the dry time is not counted as water.
  TEST_ASSERT_FLOAT_WITHIN(1.0f, f.world.intakeMl, f.world.bottleMl);
  TEST_ASSERT_FALSE(f.status().lockedOut);  // a refill is all it needs
}

void test_pump_current_lets_watering_continue_past_a_pessimistic_gauge() {
  Fixture f;
  f.learnPump();
  f.engine.refilled(0);
  f.world.flowMlPerS = 1.6f;  // slower than the profile's 2 ml/s: the gauge over-counts
  f.world.bottleMl = 1000;
  f.plant.maxDailyMl = 5000;
  f.plant.minIntervalH = 0.5f;
  f.settings.waterWindowStartH = 0;
  f.settings.waterWindowEndH = 0;
  f.reconfigure();
  f.world.plant.dryPctPerHour = 6;
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 48 * kHour);
  const NodeStatus st = f.status();
  TEST_ASSERT_TRUE(st.bottle.empty);
  TEST_ASSERT_EQUAL(EmptyReason::PumpDry, st.bottle.reason);
  // The bottle really was used up, and the gauge learned the slower pump.
  TEST_ASSERT_FLOAT_WITHIN(5.0f, f.world.intakeMl, f.world.bottleMl);
  TEST_ASSERT_TRUE(st.bottle.flowFactor < 0.95f);
}

void test_unplugged_pump_locks_out_until_cleared() {
  Fixture f;
  f.world.pumpUnplugged = true;
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 5 * kMin);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::PumpDisconnected, 0));
  TEST_ASSERT_TRUE(f.status().lockedOut);
  TEST_ASSERT_EQUAL(PumpVerdict::Disconnected, f.status().pumpVerdict);
  TEST_ASSERT_EQUAL_STRING("lockout", f.engine.manualWater(50).c_str());
  TEST_ASSERT_EQUAL_FLOAT(0, f.sink.wateredMl());
  f.world.pumpUnplugged = false;
  TEST_ASSERT_EQUAL_STRING("", f.engine.clearFaults().c_str());
  TEST_ASSERT_FALSE(f.engine.alerts().isActive(AlertCode::PumpDisconnected, 0));
  run(f.world, f.engine, 2 * kHour);
  TEST_ASSERT_TRUE(f.sink.wateredMl() > 50);
}

void test_jammed_pump_locks_out() {
  Fixture f;
  f.learnPump();
  f.world.pumpJammed = true;
  f.engine.tick(f.world.inputs());
  f.engine.manualWater(50);
  run(f.world, f.engine, 1 * kMin);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::PumpJammed, 0));
  TEST_ASSERT_TRUE(f.status().lockedOut);
}

void test_no_response_without_pump_evidence_means_bottle_empty() {
  Fixture f;
  f.plant.pump.currentSense = false;
  f.reconfigure();
  f.world.currentSensor = false;
  f.world.plant.waterReachesPot = false;  // e.g. the bottle is empty and the gauge is wrong
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 3 * kHour);
  TEST_ASSERT_TRUE(f.sink.wateredMl() <= 2 * 120 + 1);
  TEST_ASSERT_TRUE(f.status().bottle.empty);
  TEST_ASSERT_EQUAL(EmptyReason::NoResponse, f.status().bottle.reason);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::BottleEmpty, 0));
  TEST_ASSERT_FALSE(f.engine.alerts().isActive(AlertCode::SensorNoResponse, 0));
}

void test_no_response_with_a_wet_pump_blames_the_sensor() {
  Fixture f;
  f.learnPump();
  f.world.plant.sensorInSoil = false;  // reads bone dry forever
  run(f.world, f.engine, 3 * kHour);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::SensorNoResponse, 0));
  TEST_ASSERT_FALSE(f.status().bottle.empty);
  const float watered = f.sink.wateredMl();
  run(f.world, f.engine, 24 * kHour);
  TEST_ASSERT_EQUAL_FLOAT(watered, f.sink.wateredMl());  // paused
}

void test_unplugged_sensor_raises_fault_and_does_not_water() {
  Fixture f;
  f.world.plant.sensorUnplugged = true;
  run(f.world, f.engine, 1 * kHour);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::SensorFault, 0));
  TEST_ASSERT_EQUAL_FLOAT(0, f.sink.wateredMl());
}

void test_fallback_schedule_with_failed_sensor() {
  Fixture f;
  f.plant.onSensorFault = SensorFaultPolicy::FallbackSchedule;
  f.plant.fallbackEveryDays = 1;
  f.plant.fallbackMl = 100;
  f.reconfigure();
  f.world.plant.sensorUnplugged = true;
  run(f.world, f.engine, 50 * kHour);
  TEST_ASSERT_EQUAL(3, f.sink.count(EventType::Watering, "fallback"));  // at ~0 h, 24 h and 48 h
  EpochS prev = 0;
  for (const auto& e : f.sink.events) {
    if (e.type != EventType::Watering || e.code != "fallback") continue;
    if (prev) TEST_ASSERT_TRUE(e.ts - prev >= 86400);
    prev = e.ts;
  }
}

void test_interrupted_dispense_is_counted_on_restore() {
  World w;
  RecordingSink sink;
  Engine e(w, sink);
  PlantProfile p = makePlant("fern");
  e.configure(w.settings(), &p);
  PersistedState st;
  st.day = static_cast<int32_t>(w.inputs().localDay);
  st.bottle.ml = 500;
  st.dispenseInProgress = true;
  st.inProgressMl = 90;
  e.restore(st, w.inputs());
  TEST_ASSERT_TRUE(e.alerts().isActive(AlertCode::InterruptedDispense, 0));
  TEST_ASSERT_EQUAL_FLOAT(90, e.status().mlToday);
  TEST_ASSERT_EQUAL_FLOAT(410, e.status().bottle.ml);
  TEST_ASSERT_FALSE(sink.last.dispenseInProgress);
  // and the minimum interval applies from now
  w.plant.moisture = 30;
  run(w, e, 1 * kHour);
  TEST_ASSERT_EQUAL(0, sink.count(EventType::Watering, "auto"));
}

void test_daily_limit_resets_at_midnight() {
  Fixture f;
  f.world.startHour = 22;
  f.settings.waterWindowStartH = 0;
  f.settings.waterWindowEndH = 0;  // always allowed
  f.plant.maxDailyMl = 100;
  f.plant.minIntervalH = 0.5f;
  f.reconfigure();
  f.world.plant.dryPctPerHour = 8;  // a very thirsty plant
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 100 * kMin);  // until 23:40
  TEST_ASSERT_TRUE(f.status().mlToday <= 100.5f);
  TEST_ASSERT_TRUE(f.status().mlToday > 50);
  const float before = f.world.dispensedTotal;
  run(f.world, f.engine, 30 * kMin);  // past midnight: a new day's allowance
  TEST_ASSERT_TRUE(f.world.dispensedTotal > before + 20);
  TEST_ASSERT_TRUE(f.status().mlToday <= 100.5f);
}

void test_automation_pause() {
  Fixture f;
  f.engine.setAutomation(false);
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 1 * kHour);
  TEST_ASSERT_EQUAL_FLOAT(0, f.sink.wateredMl());
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater(40).c_str());  // manual still works
}

void test_new_plant_resets_runtime_but_keeps_bottle_and_pump() {
  Fixture f;
  f.learnPump();
  f.world.plant.moisture = 30;
  run(f.world, f.engine, 2 * kHour);
  TEST_ASSERT_TRUE(f.status().mlToday > 0);
  const float bottle = f.status().bottle.ml;
  PlantProfile cactus = makePlant("cactus");
  f.engine.configure(f.settings, &cactus);
  TEST_ASSERT_EQUAL_STRING("cactus", f.status().plantId.c_str());
  TEST_ASSERT_EQUAL_FLOAT(0, f.status().mlToday);
  TEST_ASSERT_EQUAL_FLOAT(bottle, f.status().bottle.ml);
  TEST_ASSERT_TRUE(f.status().pumpReady);
}

void test_battery_low_warns_and_flat_stops_the_pump() {
  Fixture f;
  f.world.batteryMv = 3550;
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::BatteryLow, -1));
  TEST_ASSERT_TRUE(f.status().batteryLow);
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater(30).c_str());  // still pumps
  run(f.world, f.engine, 2 * kMin);
  // The sag while pumping (3.25 V) did not count as flat.
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater(30).c_str());
  run(f.world, f.engine, 2 * kMin);

  f.world.batteryMv = 3200;
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_EQUAL_STRING("battery_low", f.engine.manualWater(30).c_str());
  const Alert* a = nullptr;
  for (const auto& x : f.engine.alerts().all()) {
    if (x.active && x.code == AlertCode::BatteryLow) a = &x;
  }
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL(Severity::Critical, a->severity);
  // The escalation was announced again for the main station.
  int raised = 0;
  for (const auto& e : f.sink.events) {
    if (e.type == EventType::AlertRaised && e.code == "battery_low") raised++;
  }
  TEST_ASSERT_EQUAL(2, raised);

  f.world.batteryMv = 4600;  // fresh cells
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_FALSE(f.engine.alerts().isActive(AlertCode::BatteryLow, -1));
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater(30).c_str());
}

void test_node_without_plant_stays_idle() {
  World w;
  RecordingSink sink;
  Engine e(w, sink);
  e.configure(w.settings(), nullptr);
  e.restore(PersistedState{}, w.inputs());
  w.plant.moisture = 5;
  run(w, e, 2 * kHour);
  TEST_ASSERT_FALSE(e.hasPlant());
  TEST_ASSERT_EQUAL_FLOAT(0, w.dispensedTotal);
  TEST_ASSERT_EQUAL_STRING("no_plant", e.manualWater(50).c_str());
  TEST_ASSERT_FALSE(e.status().hasPlant);
}

void test_wants_ticks_only_while_working() {
  Fixture f;
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_EQUAL(-1, f.engine.wantTickBy(f.world.now));  // idle: the regular check-in is enough
  f.engine.manualWater(20);
  TEST_ASSERT_EQUAL(f.world.now, f.engine.wantTickBy(f.world.now));  // start it now

  Fixture g;
  g.world.plant.moisture = 30;
  run(g.world, g.engine, 15000);
  TEST_ASSERT_TRUE(g.engine.doseActive());
  TEST_ASSERT_EQUAL(g.world.now + 100, g.engine.wantTickBy(g.world.now));
  run(g.world, g.engine, 2 * kMin);
  TEST_ASSERT_EQUAL(PlannerState::Soaking, g.status().planner);
  const MonoMs next = g.engine.wantTickBy(g.world.now);
  TEST_ASSERT_TRUE(next > g.world.now && next <= g.world.now + kSessionSampleMs);
}

void test_flow_runs_do_not_count_as_watering() {
  Fixture f;
  TEST_ASSERT_EQUAL_STRING("", f.engine.timedRun(10).c_str());
  run(f.world, f.engine, 1 * kMin);
  TEST_ASSERT_EQUAL_FLOAT(0, f.status().mlToday);
  TEST_ASSERT_EQUAL_INT64(0, f.status().lastWateredAt);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 980.0f, f.status().bottle.ml);  // but it left the bottle
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_waters_dry_plant_to_target);
  RUN_TEST(test_persists_before_starting_pump);
  RUN_TEST(test_manual_watering);
  RUN_TEST(test_timed_run_for_flow_calibration);
  RUN_TEST(test_station_stop_halts_pump_until_resumed);
  RUN_TEST(test_gauge_counts_down_and_refill_resets);
  RUN_TEST(test_gauge_empty_blocks_watering_without_current_sensing);
  RUN_TEST(test_pump_running_dry_marks_bottle_empty);
  RUN_TEST(test_pump_current_lets_watering_continue_past_a_pessimistic_gauge);
  RUN_TEST(test_unplugged_pump_locks_out_until_cleared);
  RUN_TEST(test_jammed_pump_locks_out);
  RUN_TEST(test_no_response_without_pump_evidence_means_bottle_empty);
  RUN_TEST(test_no_response_with_a_wet_pump_blames_the_sensor);
  RUN_TEST(test_unplugged_sensor_raises_fault_and_does_not_water);
  RUN_TEST(test_fallback_schedule_with_failed_sensor);
  RUN_TEST(test_interrupted_dispense_is_counted_on_restore);
  RUN_TEST(test_daily_limit_resets_at_midnight);
  RUN_TEST(test_automation_pause);
  RUN_TEST(test_new_plant_resets_runtime_but_keeps_bottle_and_pump);
  RUN_TEST(test_battery_low_warns_and_flat_stops_the_pump);
  RUN_TEST(test_node_without_plant_stays_idle);
  RUN_TEST(test_wants_ticks_only_while_working);
  RUN_TEST(test_flow_runs_do_not_count_as_watering);
  return UNITY_END();
}
