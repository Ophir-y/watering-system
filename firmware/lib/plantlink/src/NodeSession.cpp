#include "NodeSession.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace plantlink {

namespace {
// Serial-number arithmetic: a is newer than b (works across the u16 wrap).
bool newer16(uint16_t a, uint16_t b) { return static_cast<int16_t>(a - b) > 0; }
}  // namespace

NodeSession::NodeSession(INodeApp& app, const Mac& self, uint8_t board, std::string fw)
    : app_(app), self_(self), board_(board), fw_(std::move(fw)) {}

void NodeSession::restore(const Pairing& p) {
  p_ = p;
  // Everything up to the reserved number may have been used before the reboot.
  seq_ = p_.seq;
}

uint32_t NodeSession::nextSeq() {
  ++seq_;
  if (seq_ >= p_.seq) {
    p_.seq = seq_ + kSeqReserve;
    dirty();
  }
  return seq_;
}

void NodeSession::queueEvent(const plantcore::Event& e) {
  if (!p_.paired) return;  // nobody to tell yet
  if (outbox_.size() >= kOutboxMax) outbox_.pop_front();
  outbox_.push_back(e);
}

void NodeSession::setChannel(uint8_t ch) {
  if (p_.channel == ch) return;
  p_.channel = ch;
  dirty();
}

uint8_t NodeSession::linkPct() const {
  if (linkN_ == 0) return 255;
  int ok = 0;
  for (int i = 0; i < linkN_; ++i) ok += (linkBits_ >> i) & 1u;
  return static_cast<uint8_t>(ok * 100 / linkN_);
}

void NodeSession::recordLink(bool ok) {
  linkBits_ = (linkBits_ << 1) | (ok ? 1u : 0u);
  if (linkN_ < 32) linkN_++;
}

NodeAck NodeSession::ack() const {
  NodeAck a;
  a.cfgVersion = p_.cfgVersion;
  a.cfgRxVersion = rxVersion_;
  a.cfgRxOffset = static_cast<uint16_t>(rxBuf_.size());
  a.lastCmdId = p_.lastCmdId;
  return a;
}

void NodeSession::address(OutFrame& out) const {
  out.broadcast = !p_.haveHub;
  out.to = p_.haveHub ? p_.hub : Mac{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
}

void NodeSession::buildHello(OutFrame& out) {
  HelloMsg m;
  m.board = board_;
  m.fw = fw_;
  currentType_ = MsgType::Hello;
  currentSeq_ = nextSeq();
  out.len = encode(m, currentSeq_, out.data);
  address(out);
}

void NodeSession::buildReport(OutFrame& out) {
  ReportMsg m;
  app_.fillReport(m);
  m.ack = ack();
  m.fw = fw_;
  m.linkPct = linkPct();
  m.outbox = static_cast<uint8_t>(std::min<size_t>(outbox_.size(), 255));
  m.button = button_;
  button_ = false;
  currentType_ = MsgType::Report;
  currentSeq_ = nextSeq();
  out.len = encode(m, currentSeq_, p_.key, self_, out.data);
  address(out);
}

void NodeSession::buildEvent(OutFrame& out) {
  EventMsg m;
  m.ack = ack();
  m.event = outbox_.front();
  currentType_ = MsgType::Event;
  currentSeq_ = nextSeq();
  out.len = encode(m, currentSeq_, p_.key, self_, out.data);
  address(out);
}

void NodeSession::buildPoll(OutFrame& out) {
  PollMsg m;
  m.ack = ack();
  currentType_ = MsgType::Poll;
  currentSeq_ = nextSeq();
  out.len = encode(m, currentSeq_, p_.key, self_, out.data);
  address(out);
}

void NodeSession::start(OutFrame& out) {
  frames_ = 1;
  retries_ = 0;
  gotReply_ = false;
  if (p_.paired) {
    buildReport(out);
  } else {
    buildHello(out);
  }
  current_ = out;
}

Step NodeSession::finish(bool ok) {
  if (gotReply_) {
    missed_ = 0;
  } else if (!ok) {
    missed_++;
  }
  return Step::Done;
}

void NodeSession::unpair() {
  p_.paired = false;
  p_.key = Key{};
  p_.lastCmdId = 0;
  p_.cfgVersion = 0;
  rxVersion_ = 0;
  rxBuf_.clear();
  outbox_.clear();
  rejects_ = 0;
  dirty();
  app_.onUnpaired();
}

void NodeSession::handleChunk(const ConfigChunk& c) {
  if (c.version == 0 || c.version == p_.cfgVersion) return;  // already running it
  if (c.version != rxVersion_) {
    if (c.offset != 0) return;  // wait for the station to start over
    rxVersion_ = c.version;
    rxBuf_.clear();
  }
  if (c.offset != rxBuf_.size()) return;  // a resend of a piece we have
  rxBuf_ += c.data;
  if (rxBuf_.size() < c.total) return;
  if (rxBuf_.size() == c.total) app_.onConfig(rxBuf_);
  // Taken either way: a config the node cannot use is not asked for again.
  p_.cfgVersion = c.version;
  rxVersion_ = 0;
  rxBuf_.clear();
  dirty();
}

Step NodeSession::onReceive(const Mac& from, const uint8_t* d, size_t n, OutFrame& out) {
  Header h;
  if (!parseHeader(d, n, h)) return Step::Wait;
  if (p_.haveHub && from != p_.hub && h.type != MsgType::Welcome) return Step::Wait;

  if (!p_.paired) {
    WelcomeMsg w;
    if (currentType_ != MsgType::Hello || h.type != MsgType::Welcome || h.seq != currentSeq_ || !decode(d, n, w)) {
      return Step::Wait;
    }
    recordLink(true);
    gotReply_ = true;
    p_.haveHub = true;
    p_.hub = from;
    if (w.channel) p_.channel = w.channel;
    p_.checkInS = w.checkInS ? w.checkInS : p_.checkInS;
    if (w.adopted) {
      p_.paired = true;
      p_.key = w.key;
      p_.lastCmdId = 0;
      p_.cfgVersion = 0;
    }
    dirty();
    if (!w.adopted) return finish(true);
    // Paired: report right away so the dashboard fills in.
    frames_++;
    buildReport(out);
    current_ = out;
    retries_ = 0;
    return Step::Send;
  }

  if (h.type == MsgType::Reject) {
    RejectMsg r;
    if (!decode(d, n, r) || h.seq != currentSeq_) return Step::Wait;
    recordLink(true);
    gotReply_ = true;
    if (++rejects_ >= kRejectsToUnpair) unpair();
    return finish(true);
  }

  ReplyMsg r;
  if (h.type != MsgType::Reply || h.seq != currentSeq_ || !checkTag(d, n, p_.key, self_) || !decode(d, n, r)) {
    return Step::Wait;
  }
  recordLink(true);
  gotReply_ = true;
  rejects_ = 0;
  if (currentType_ == MsgType::Event && !outbox_.empty()) outbox_.pop_front();

  app_.onStationState(r);
  // Whatever this reply carried is acknowledged by the next frame; if there would be
  // none, a last Poll does it, so the dashboard stops showing it as queued.
  const bool needAck = r.item != ReplyItem::None;
  if (r.item == ReplyItem::Command && newer16(r.command.id, p_.lastCmdId)) {
    p_.lastCmdId = r.command.id;
    dirty();
    if (r.command.kind == CommandKind::Forget) {
      unpair();
      return finish(true);
    }
    app_.onCommand(r.command);
  } else if (r.item == ReplyItem::Config) {
    handleChunk(r.chunk);
  }

  if (frames_ >= kMaxFramesPerCheckIn) return finish(true);
  if (!outbox_.empty()) {
    buildEvent(out);
  } else if (r.more || needAck) {
    buildPoll(out);
  } else {
    return finish(true);
  }
  frames_++;
  current_ = out;
  retries_ = 0;
  return Step::Send;
}

Step NodeSession::onTimeout(OutFrame& out) {
  recordLink(false);
  if (retries_ < kMaxRetries) {
    retries_++;
    out = current_;  // same seq: the station answers a repeat with the same reply
    return Step::Send;
  }
  return finish(false);
}

}  // namespace plantlink
