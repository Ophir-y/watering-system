#include <unity.h>

#include "../sim.h"

using namespace plantcore;
using namespace sim;

void setUp() {}
void tearDown() {}

static const MonoMs kMin = 60000;
static const MonoMs kHour = 60 * kMin;

struct Fixture {
  World world{2};
  RecordingSink sink;
  Engine engine{world, sink};
  Settings settings;
  std::vector<PlantProfile> plants{makePlant("fern", 0), makePlant("pothos", 1)};

  Fixture() {
    world.plants[0].moisture = 50;
    world.plants[1].moisture = 50;
    engine.configure(settings, plants);
    engine.restore(PersistedState{}, world.inputs());
    world.lastSampleMs = -1000000;  // let the first tick take a fresh sample
  }
  const PlantStatus& status(int i) {
    static EngineStatus st;
    st = engine.status();
    return st.plants[i];
  }
};

void test_waters_dry_plant_to_target() {
  Fixture f;
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 2 * kHour);
  TEST_ASSERT_TRUE(f.sink.wateredMl(0) > 100);
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 55.0f, f.world.plants[0].moisture);
  TEST_ASSERT_EQUAL_FLOAT(0, f.sink.wateredMl(1));  // the other plant was fine
  TEST_ASSERT_EQUAL(1, f.sink.count(EventType::Watering, "auto"));
  TEST_ASSERT_TRUE(f.status(0).mlPerPct > 0);  // learned
  TEST_ASSERT_TRUE(f.status(0).lastWateredAt > 0);
}

void test_persists_before_opening_outlet() {
  struct CheckingSink : RecordingSink {
    World* world = nullptr;
    bool sawOpenWhilePersisting = false;
    void onPersist(const PersistedState& s) override {
      if (s.dispenseInProgress && world->openChannel >= 0) sawOpenWhilePersisting = true;
      RecordingSink::onPersist(s);
    }
  };
  World w(1);
  CheckingSink sink;
  sink.world = &w;
  Engine e(w, sink);
  w.plants[0].moisture = 30;
  e.configure(Settings{}, {makePlant("fern", 0)});
  e.restore(PersistedState{}, w.inputs());
  run(w, e, 2 * kMin);
  TEST_ASSERT_TRUE(e.doseActive() || sink.wateredMl() > 0 || sink.persists > 0);
  TEST_ASSERT_FALSE(sink.sawOpenWhilePersisting);
}

void test_manual_watering() {
  Fixture f;
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater("pothos", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("busy", f.engine.manualWater("fern", 50).c_str());
  run(f.world, f.engine, 2 * kMin);
  TEST_ASSERT_FLOAT_WITHIN(3.0f, 80.0f, f.sink.wateredMl(1));
  TEST_ASSERT_EQUAL(1, f.sink.count(EventType::Watering, "manual"));
  TEST_ASSERT_EQUAL_STRING("not_found", f.engine.manualWater("nope", 50).c_str());
}

void test_leak_stops_everything_until_resume() {
  Fixture f;
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 30000);  // first pulse running
  TEST_ASSERT_TRUE(f.engine.doseActive());
  f.world.leak = true;
  run(f.world, f.engine, 1000);
  TEST_ASSERT_EQUAL(-1, f.world.openChannel);
  TEST_ASSERT_TRUE(f.engine.status().emergencyStop);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::LeakDetected, -1));
  TEST_ASSERT_EQUAL_STRING("leak", f.engine.manualWater("fern", 50).c_str());
  TEST_ASSERT_EQUAL_STRING("leak_still_detected", f.engine.resume().c_str());

  f.world.leak = false;
  run(f.world, f.engine, 1000);
  TEST_ASSERT_TRUE(f.engine.status().emergencyStop);  // still latched
  TEST_ASSERT_EQUAL_STRING("", f.engine.resume().c_str());
  TEST_ASSERT_FALSE(f.engine.status().emergencyStop);
}

void test_reservoir_empty_blocks_watering() {
  Fixture f;
  f.world.reservoirMl = 80;  // float switch trips below 100
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 1 * kHour);
  TEST_ASSERT_EQUAL_FLOAT(0, f.sink.wateredMl());
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::ReservoirEmpty, -1));
  TEST_ASSERT_EQUAL_STRING("reservoir_empty", f.engine.manualWater("fern", 50).c_str());
}

void test_reservoir_running_dry_mid_dose_stops_outlet() {
  Fixture f;
  f.world.reservoirMl = 200;  // enough to start a 100 ml pulse, not to finish it above 150 ml
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 3 * kMin);
  TEST_ASSERT_EQUAL(-1, f.world.openChannel);
  TEST_ASSERT_FLOAT_WITHIN(10.0f, 150.0f, f.world.reservoirMl);  // stopped at the empty threshold
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::ReservoirEmpty, -1));
}

void test_broken_outlet_is_locked_out_until_cleared() {
  Fixture f;
  f.world.outletBroken[0] = true;
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 5 * kMin);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::OutletNoFlow, 0));
  TEST_ASSERT_TRUE(f.status(0).lockedOut);
  TEST_ASSERT_EQUAL_STRING("lockout", f.engine.manualWater("fern", 50).c_str());
  f.world.outletBroken[0] = false;
  TEST_ASSERT_EQUAL_STRING("", f.engine.clearLockout("fern").c_str());
  TEST_ASSERT_FALSE(f.engine.alerts().isActive(AlertCode::OutletNoFlow, 0));
  run(f.world, f.engine, 2 * kHour);
  TEST_ASSERT_TRUE(f.sink.wateredMl(0) > 50);
}

void test_stuck_open_valve_triggers_emergency_stop() {
  Fixture f;
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 20000);
  TEST_ASSERT_TRUE(f.engine.doseActive());
  f.world.stuckOpen[0] = true;
  run(f.world, f.engine, 3 * kMin);
  TEST_ASSERT_TRUE(f.engine.status().emergencyStop);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::OutletStuckOpen, 0));
}

void test_sensor_out_of_soil_stops_after_no_response() {
  Fixture f;
  f.world.plants[0].sensorInSoil = false;  // reads bone dry forever
  run(f.world, f.engine, 3 * kHour);
  // two pulses at most, then the session gives up and the plant is paused
  TEST_ASSERT_TRUE(f.sink.wateredMl(0) <= 2 * 120 + 1);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::SensorNoResponse, 0));
  run(f.world, f.engine, 24 * kHour);
  TEST_ASSERT_TRUE(f.sink.wateredMl(0) <= 2 * 120 + 1);
}

void test_unplugged_sensor_raises_fault_and_does_not_water() {
  Fixture f;
  f.world.plants[0].sensorUnplugged = true;
  run(f.world, f.engine, 1 * kHour);
  TEST_ASSERT_TRUE(f.engine.alerts().isActive(AlertCode::SensorFault, 0));
  TEST_ASSERT_EQUAL_FLOAT(0, f.sink.wateredMl(0));
}

void test_fallback_schedule_with_failed_sensor() {
  Fixture f;
  f.plants[0].onSensorFault = SensorFaultPolicy::FallbackSchedule;
  f.plants[0].fallbackEveryDays = 1;
  f.plants[0].fallbackMl = 100;
  f.engine.configure(f.settings, f.plants);
  f.world.plants[0].sensorUnplugged = true;
  run(f.world, f.engine, 50 * kHour);
  int n = f.sink.count(EventType::Watering, "fallback");
  TEST_ASSERT_EQUAL(3, n);  // at ~0 h, 24 h and 48 h
  // never twice within the interval
  EpochS prev = 0;
  for (const auto& e : f.sink.events) {
    if (e.type != EventType::Watering || e.code != "fallback") continue;
    if (prev) TEST_ASSERT_TRUE(e.ts - prev >= 86400);
    prev = e.ts;
  }
}

void test_interrupted_dispense_is_counted_on_restore() {
  World w(1);
  RecordingSink sink;
  Engine e(w, sink);
  e.configure(Settings{}, {makePlant("fern", 0)});
  PersistedState st;
  st.day = static_cast<int32_t>(w.inputs().localDay);
  st.dispenseInProgress = true;
  st.inProgressChannel = 0;
  st.inProgressMl = 90;
  e.restore(st, w.inputs());
  TEST_ASSERT_TRUE(e.alerts().isActive(AlertCode::InterruptedDispense, 0));
  TEST_ASSERT_EQUAL_FLOAT(90, e.status().plants[0].mlToday);
  TEST_ASSERT_FALSE(sink.last.dispenseInProgress);
  // and the minimum interval applies from now
  w.plants[0].moisture = 30;
  run(w, e, 1 * kHour);
  TEST_ASSERT_EQUAL(0, sink.count(EventType::Watering, "auto"));
}

void test_daily_limit_resets_at_midnight() {
  Fixture f;
  f.world.startHour = 22;
  f.settings.waterWindowStartH = 0;
  f.settings.waterWindowEndH = 0;  // always allowed
  f.plants[0].maxDailyMl = 100;
  f.plants[0].minIntervalH = 0.5f;
  f.engine.configure(f.settings, f.plants);
  f.world.plants[0].dryPctPerHour = 8;  // a very thirsty plant
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 100 * kMin);  // until 23:40
  TEST_ASSERT_TRUE(f.status(0).mlToday <= 100.5f);
  TEST_ASSERT_TRUE(f.status(0).mlToday > 50);
  run(f.world, f.engine, 30 * kMin);  // past midnight
  TEST_ASSERT_TRUE(f.status(0).mlToday < 100);
}

void test_automation_pause() {
  Fixture f;
  f.engine.setAutomation(false);
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 1 * kHour);
  TEST_ASSERT_EQUAL_FLOAT(0, f.sink.wateredMl());
  TEST_ASSERT_EQUAL_STRING("", f.engine.manualWater("fern", 40).c_str());  // manual still works
}

void test_configure_with_new_plant_on_channel_resets_runtime() {
  Fixture f;
  f.world.plants[0].moisture = 30;
  run(f.world, f.engine, 2 * kHour);
  TEST_ASSERT_TRUE(f.status(0).mlToday > 0);
  f.plants[0] = makePlant("cactus", 0);
  f.engine.configure(f.settings, f.plants);
  TEST_ASSERT_EQUAL_STRING("cactus", f.status(0).id.c_str());
  TEST_ASSERT_EQUAL_FLOAT(0, f.status(0).mlToday);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_waters_dry_plant_to_target);
  RUN_TEST(test_persists_before_opening_outlet);
  RUN_TEST(test_manual_watering);
  RUN_TEST(test_leak_stops_everything_until_resume);
  RUN_TEST(test_reservoir_empty_blocks_watering);
  RUN_TEST(test_reservoir_running_dry_mid_dose_stops_outlet);
  RUN_TEST(test_broken_outlet_is_locked_out_until_cleared);
  RUN_TEST(test_stuck_open_valve_triggers_emergency_stop);
  RUN_TEST(test_sensor_out_of_soil_stops_after_no_response);
  RUN_TEST(test_unplugged_sensor_raises_fault_and_does_not_water);
  RUN_TEST(test_fallback_schedule_with_failed_sensor);
  RUN_TEST(test_interrupted_dispense_is_counted_on_restore);
  RUN_TEST(test_daily_limit_resets_at_midnight);
  RUN_TEST(test_automation_pause);
  RUN_TEST(test_configure_with_new_plant_on_channel_resets_runtime);
  return UNITY_END();
}
