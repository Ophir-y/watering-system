// The plant node's side of the radio conversation. Transport-free: the firmware
// sends the frames it is given and feeds back what it hears, so the whole protocol
// runs in host tests.
//
// One check-in:
//   start(out) -> send -> onReceive(...) / onTimeout(...) -> Send (again) | Wait | Done
//
// Unpaired, the node says Hello until the station welcomes it; once you add the
// plant in the dashboard, the next Welcome carries the key and the node is paired.
// Paired, it sends a Report, then its queued events, then polls while the station
// says it has more (a command or the pieces of a new config).
#pragma once

#include <deque>
#include <string>

#include "Frames.h"

namespace plantlink {

class INodeApp {
 public:
  virtual ~INodeApp() = default;
  // Telemetry for the next Report (the session fills ack, fw, link and outbox).
  virtual void fillReport(ReportMsg& r) = 0;
  // Every Reply: the station's clock, room climate, stop and pause flags.
  virtual void onStationState(const ReplyMsg& r) = 0;
  // A new command (each id is delivered once).
  virtual void onCommand(const Command& c) = 0;
  // A complete config. Return false if it could not be applied.
  virtual bool onConfig(const std::string& json) = 0;
  // Pairing() changed: persist it before the next frame goes out.
  virtual void onPairingChanged() = 0;
  // The station forgot this node (it will say Hello again).
  virtual void onUnpaired() = 0;
};

struct Pairing {
  bool haveHub = false;  // the station's MAC and channel are known
  Mac hub{};
  uint8_t channel = 1;
  bool paired = false;
  Key key{};
  uint32_t seq = 0;          // reserved sequence number: never reused after a reboot
  uint16_t lastCmdId = 0;
  uint32_t cfgVersion = 0;
  uint16_t checkInS = 300;
};

struct OutFrame {
  Mac to{};
  bool broadcast = false;
  size_t len = 0;
  uint8_t data[kMaxFrame];
};

enum class Step : uint8_t { Send, Wait, Done };

constexpr int kMaxRetries = 2;          // resends of one frame before giving up
constexpr int kMaxFramesPerCheckIn = 24;
constexpr uint32_t kSeqReserve = 64;    // seq numbers reserved per flash write
constexpr int kRejectsToUnpair = 3;
constexpr size_t kOutboxMax = 24;

class NodeSession {
 public:
  NodeSession(INodeApp& app, const Mac& self, uint8_t board, std::string fw);

  void restore(const Pairing& p);
  const Pairing& pairing() const { return p_; }
  bool paired() const { return p_.paired; }
  bool haveHub() const { return p_.haveHub; }
  uint8_t channel() const { return p_.channel; }
  uint16_t checkInS() const { return p_.checkInS; }

  void queueEvent(const plantcore::Event& e);
  size_t outbox() const { return outbox_.size(); }
  void pressButton() { button_ = true; }

  void start(OutFrame& out);
  Step onReceive(const Mac& from, const uint8_t* d, size_t n, OutFrame& out);
  Step onTimeout(OutFrame& out);

  // While scanning for the station: the reply came on this channel.
  void setChannel(uint8_t ch);
  // Check-ins in a row that got no reply at all (time to scan the channels).
  int missedInARow() const { return missed_; }
  uint8_t linkPct() const;

 private:
  uint32_t nextSeq();
  void dirty() { app_.onPairingChanged(); }
  void buildHello(OutFrame& out);
  void buildReport(OutFrame& out);
  void buildEvent(OutFrame& out);
  void buildPoll(OutFrame& out);
  void address(OutFrame& out) const;
  NodeAck ack() const;
  Step finish(bool ok);
  void unpair();
  void handleChunk(const ConfigChunk& c);
  void recordLink(bool ok);

  INodeApp& app_;
  Mac self_;
  uint8_t board_;
  std::string fw_;
  Pairing p_;
  uint32_t seq_ = 0;

  // The frame in flight.
  OutFrame current_;
  MsgType currentType_ = MsgType::Hello;
  uint32_t currentSeq_ = 0;
  int retries_ = 0;
  int frames_ = 0;
  bool gotReply_ = false;

  std::deque<plantcore::Event> outbox_;
  bool button_ = false;
  int rejects_ = 0;
  int missed_ = 0;
  uint32_t linkBits_ = 0;  // 1 = an attempt that got its reply, newest in bit 0
  int linkN_ = 0;

  uint32_t rxVersion_ = 0;
  std::string rxBuf_;
};

}  // namespace plantlink
