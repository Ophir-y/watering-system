// Node sessions talking to the station's fleet over a lossy in-memory radio.
#include <unity.h>

#include "../link_sim.h"

using namespace plantlink;
using namespace linksim;

void setUp() {}
void tearDown() {}

struct Net {
  RecordingFleetSink sink;
  Fleet fleet{sink};
  ReplyContext ctx;
  int64_t now = 1000;
  StationFn station() {
    return [this](const Mac& from, const uint8_t* d, size_t n, uint8_t* out) {
      return fleet.onFrame(from, d, n, now, ctx, out);
    };
  }
  Net() {
    ctx.channel = 6;
    ctx.checkInS = 300;
    ctx.epoch = 1767254400;
  }
};

// Pairs a node: Hello, the user adds it, Hello again gets the key.
static void pair(Net& net, SimNode& node, uint8_t seed = 1) {
  checkIn(node, net.station());
  TEST_ASSERT_TRUE(net.fleet.adopt(macToId(node.mac), testKey(seed)));
  checkIn(node, net.station());
  TEST_ASSERT_TRUE(node.session.paired());
}

void test_pairing_flow() {
  Net net;
  SimNode node(1);
  CheckIn c = checkIn(node, net.station());
  TEST_ASSERT_TRUE(c.done);
  TEST_ASSERT_TRUE(node.session.haveHub());
  TEST_ASSERT_FALSE(node.session.paired());
  TEST_ASSERT_EQUAL(6, node.session.channel());
  TEST_ASSERT_EQUAL(1, net.sink.hellos);
  const NodeView* v = net.fleet.find("a0b1c2d3e401");
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_FALSE(v->adopted);

  // Still waiting: the node keeps saying Hello, nothing else happens.
  checkIn(node, net.station());
  TEST_ASSERT_FALSE(node.session.paired());

  TEST_ASSERT_TRUE(net.fleet.adopt("a0b1c2d3e401", testKey(1)));
  TEST_ASSERT_EQUAL(1, net.fleet.records().size());
  TEST_ASSERT_TRUE(net.fleet.records()[0].keyPending);
  c = checkIn(node, net.station());
  TEST_ASSERT_TRUE(node.session.paired());
  TEST_ASSERT_EQUAL(1, net.sink.reports);  // reported straight after pairing
  TEST_ASSERT_FALSE(net.fleet.records()[0].keyPending);
  TEST_ASSERT_TRUE(node.session.pairing().key == testKey(1));
}

void test_key_is_handed_over_only_once() {
  Net net;
  SimNode node(1);
  pair(net, node);
  // Someone copies the node's MAC and says Hello: no key for them.
  SimNode copycat(1);
  checkIn(copycat, net.station());
  TEST_ASSERT_FALSE(copycat.session.paired());
  TEST_ASSERT_TRUE(net.fleet.find("a0b1c2d3e401")->wantsRepair);
  // The real node's next report clears the request.
  checkIn(node, net.station());
  TEST_ASSERT_FALSE(net.fleet.find("a0b1c2d3e401")->wantsRepair);
}

void test_command_runs_once_even_when_the_reply_is_lost() {
  Net net;
  SimNode node(1);
  pair(net, node);
  const uint16_t id = net.fleet.queue("a0b1c2d3e401", CommandKind::Water, 80);
  TEST_ASSERT_TRUE(id > 0);
  const int reports = net.sink.reports;
  // The first reply (carrying the command) is lost twice: the node repeats its
  // report and gets the very same reply back.
  CheckIn c = checkIn(node, net.station(), [](int i) { return i == 1 || i == 3; });
  TEST_ASSERT_TRUE(c.done);
  TEST_ASSERT_EQUAL(1, node.app.commands.size());
  TEST_ASSERT_EQUAL(CommandKind::Water, node.app.commands[0].kind);
  TEST_ASSERT_EQUAL_FLOAT(80, node.app.commands[0].arg);
  TEST_ASSERT_EQUAL(reports + 1, net.sink.reports);  // the repeats were not counted as new reports
  TEST_ASSERT_TRUE(net.fleet.find("a0b1c2d3e401")->queue.empty());  // acknowledged by a last poll
  checkIn(node, net.station());
  TEST_ASSERT_EQUAL(1, node.app.commands.size());
}

void test_several_commands_in_one_check_in() {
  Net net;
  SimNode node(1);
  pair(net, node);
  net.fleet.queue("a0b1c2d3e401", CommandKind::Refilled, 0);
  net.fleet.queue("a0b1c2d3e401", CommandKind::ClearFaults, 0);
  net.fleet.queue("a0b1c2d3e401", CommandKind::Water, 50);
  checkIn(node, net.station());
  TEST_ASSERT_EQUAL(3, node.app.commands.size());
  TEST_ASSERT_EQUAL(CommandKind::Refilled, node.app.commands[0].kind);
  TEST_ASSERT_EQUAL(CommandKind::Water, node.app.commands[2].kind);
  TEST_ASSERT_TRUE(net.fleet.find("a0b1c2d3e401")->queue.empty());  // acked by the polls
}

void test_config_arrives_in_pieces_and_resumes() {
  Net net;
  SimNode node(1);
  pair(net, node);
  std::string json = "{\"plant\":{\"name\":\"" + std::string(600, 'x') + "\"},\"settings\":{}}";
  net.fleet.setConfig("a0b1c2d3e401", json);
  TEST_ASSERT_TRUE(net.fleet.find("a0b1c2d3e401")->configPending());
  // The radio fails after a couple of pieces.
  checkIn(node, net.station(), [](int i) { return i >= 6; });
  TEST_ASSERT_EQUAL(0, node.app.configs.size());
  TEST_ASSERT_TRUE(node.session.pairing().cfgVersion == 0);
  // Next check-in picks up where it stopped.
  checkIn(node, net.station());
  TEST_ASSERT_EQUAL(1, node.app.configs.size());
  TEST_ASSERT_EQUAL_STRING(json.c_str(), node.app.configs[0].c_str());
  checkIn(node, net.station());
  TEST_ASSERT_FALSE(net.fleet.find("a0b1c2d3e401")->configPending());
  // The same config again is not resent; a change is.
  net.fleet.setConfig("a0b1c2d3e401", json);
  TEST_ASSERT_FALSE(net.fleet.find("a0b1c2d3e401")->configPending());
  net.fleet.setConfig("a0b1c2d3e401", json + " ");
  checkIn(node, net.station());
  TEST_ASSERT_EQUAL(2, node.app.configs.size());
}

void test_events_are_delivered_once_and_in_order() {
  Net net;
  SimNode node(1);
  pair(net, node);
  for (int i = 0; i < 3; ++i) {
    plantcore::Event e;
    e.type = plantcore::EventType::Watering;
    e.code = "auto";
    e.ml = 10.0f * (i + 1);
    node.session.queueEvent(e);
  }
  // Lose the reply to the first event once.
  checkIn(node, net.station(), [](int i) { return i == 3; });
  TEST_ASSERT_EQUAL(3, net.sink.events.size());
  TEST_ASSERT_EQUAL_FLOAT(10, net.sink.events[0].ml);
  TEST_ASSERT_EQUAL_FLOAT(30, net.sink.events[2].ml);
  TEST_ASSERT_EQUAL(0, node.session.outbox());
}

void test_unknown_node_is_rejected_and_starts_over() {
  Net net;
  SimNode node(1);
  pair(net, node);
  Net fresh;  // a station that never heard of it (e.g. its flash was erased)
  for (int i = 0; i < kRejectsToUnpair; ++i) checkIn(node, fresh.station());
  TEST_ASSERT_FALSE(node.session.paired());
  TEST_ASSERT_EQUAL(1, node.app.unpaired);
  checkIn(node, fresh.station());
  TEST_ASSERT_NOT_NULL(fresh.fleet.find("a0b1c2d3e401"));  // says Hello: shows up as new
}

void test_forget_unpairs_the_node() {
  Net net;
  SimNode node(1);
  pair(net, node);
  TEST_ASSERT_TRUE(net.fleet.forget("a0b1c2d3e401"));
  checkIn(node, net.station());
  TEST_ASSERT_FALSE(node.session.paired());
  TEST_ASSERT_EQUAL(1, node.app.unpaired);
  TEST_ASSERT_NULL(net.fleet.find("a0b1c2d3e401"));
  TEST_ASSERT_EQUAL(1, net.sink.forgotten.size());
  TEST_ASSERT_EQUAL(0, net.fleet.records().size());
}

void test_replayed_frames_are_ignored() {
  Net net;
  SimNode node(1);
  pair(net, node);
  // Capture a report off the air.
  OutFrame old;
  node.session.start(old);
  uint8_t out[kMaxFrame];
  TEST_ASSERT_TRUE(net.fleet.onFrame(node.mac, old.data, old.len, net.now, net.ctx, out) > 0);
  checkIn(node, net.station());  // newer traffic
  const int reports = net.sink.reports;
  TEST_ASSERT_EQUAL(0, net.fleet.onFrame(node.mac, old.data, old.len, net.now, net.ctx, out));
  TEST_ASSERT_EQUAL(reports, net.sink.reports);
  // A forged frame (wrong key) is ignored silently.
  SimNode forger(1);
  Pairing p;
  p.paired = p.haveHub = true;
  p.hub = kHubMac;
  p.key = testKey(9);
  p.seq = 1u << 30;
  forger.session.restore(p);
  checkIn(forger, net.station());
  TEST_ASSERT_EQUAL(reports, net.sink.reports);
}

void test_station_restart_keeps_nodes_and_renumbers_commands() {
  Net net;
  SimNode node(1);
  pair(net, node);
  net.fleet.queue("a0b1c2d3e401", CommandKind::Water, 30);
  net.fleet.queue("a0b1c2d3e401", CommandKind::Water, 40);
  checkIn(node, net.station());
  checkIn(node, net.station());
  TEST_ASSERT_EQUAL(2, node.app.commands.size());

  Net restarted;
  restarted.fleet.restore(net.fleet.records());
  // A command queued before the node has been heard from since the restart: its
  // id would start at 1 again, which the node has already seen.
  restarted.fleet.queue("a0b1c2d3e401", CommandKind::Water, 55);
  checkIn(node, restarted.station());
  TEST_ASSERT_EQUAL(3, node.app.commands.size());
  TEST_ASSERT_EQUAL_FLOAT(55, node.app.commands[2].arg);
}

void test_node_restart_never_reuses_sequence_numbers() {
  Net net;
  SimNode node(1);
  pair(net, node);
  for (int i = 0; i < 5; ++i) checkIn(node, net.station());
  const Pairing saved = node.session.pairing();  // what the node had written to flash
  SimNode rebooted(1);
  rebooted.session.restore(saved);
  const int reports = net.sink.reports;
  checkIn(rebooted, net.station());
  TEST_ASSERT_EQUAL(reports + 1, net.sink.reports);
}

void test_silent_station_counts_misses_and_link_quality() {
  Net net;
  SimNode node(1);
  pair(net, node);
  checkIn(node, net.station());
  StationFn silent = [](const Mac&, const uint8_t*, size_t, uint8_t*) { return size_t(0); };
  for (int i = 0; i < 3; ++i) checkIn(node, silent);
  TEST_ASSERT_EQUAL(3, node.session.missedInARow());
  TEST_ASSERT_TRUE(node.session.linkPct() < 60);
  checkIn(node, net.station());
  TEST_ASSERT_EQUAL(0, node.session.missedInARow());
}

void test_offline_detection_and_waiting_nodes_expire() {
  Net net;
  SimNode node(1);
  pair(net, node);
  SimNode stranger(2);
  checkIn(stranger, net.station());
  net.fleet.tick(net.now, 300);
  TEST_ASSERT_TRUE(net.fleet.find("a0b1c2d3e401")->online);
  net.now += 3 * 300 * 1000 + 61000;
  net.fleet.tick(net.now, 300);
  TEST_ASSERT_FALSE(net.fleet.find("a0b1c2d3e401")->online);
  checkIn(node, net.station());
  net.fleet.tick(net.now, 300);
  TEST_ASSERT_TRUE(net.fleet.find("a0b1c2d3e401")->online);
  TEST_ASSERT_NOT_NULL(net.fleet.find("a0b1c2d3e402"));
  net.now += kPendingForgetMs;
  net.fleet.tick(net.now, 300);
  TEST_ASSERT_NULL(net.fleet.find("a0b1c2d3e402"));  // never added, gone quiet
  TEST_ASSERT_NOT_NULL(net.fleet.find("a0b1c2d3e401"));
}

void test_eta_follows_the_last_check_in() {
  Net net;
  SimNode node(1);
  pair(net, node);
  TEST_ASSERT_EQUAL(300, net.fleet.etaS("a0b1c2d3e401", net.now, 300));
  TEST_ASSERT_EQUAL(100, net.fleet.etaS("a0b1c2d3e401", net.now + 200000, 300));
  TEST_ASSERT_EQUAL(0, net.fleet.etaS("a0b1c2d3e401", net.now + 400000, 300));
  TEST_ASSERT_EQUAL(-1, net.fleet.etaS("nope", net.now, 300));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pairing_flow);
  RUN_TEST(test_key_is_handed_over_only_once);
  RUN_TEST(test_command_runs_once_even_when_the_reply_is_lost);
  RUN_TEST(test_several_commands_in_one_check_in);
  RUN_TEST(test_config_arrives_in_pieces_and_resumes);
  RUN_TEST(test_events_are_delivered_once_and_in_order);
  RUN_TEST(test_unknown_node_is_rejected_and_starts_over);
  RUN_TEST(test_forget_unpairs_the_node);
  RUN_TEST(test_replayed_frames_are_ignored);
  RUN_TEST(test_station_restart_keeps_nodes_and_renumbers_commands);
  RUN_TEST(test_node_restart_never_reuses_sequence_numbers);
  RUN_TEST(test_silent_station_counts_misses_and_link_quality);
  RUN_TEST(test_offline_detection_and_waiting_nodes_expire);
  RUN_TEST(test_eta_follows_the_last_check_in);
  return UNITY_END();
}
