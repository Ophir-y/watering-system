#include "Fleet.h"

#include <algorithm>

namespace plantlink {

namespace {
bool newer16(uint16_t a, uint16_t b) { return static_cast<int16_t>(a - b) > 0; }
bool newer32(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }
}  // namespace

uint32_t fnv1a(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h ? h : 1;
}

void Fleet::restore(const std::vector<NodeRecord>& records) {
  nodes_.clear();
  for (const auto& r : records) {
    Mac mac;
    if (!idToMac(r.id, mac)) continue;
    NodeView n;
    n.id = r.id;
    n.mac = mac;
    n.adopted = true;
    n.keyPending = r.keyPending;
    n.key = r.key;
    nodes_.push_back(n);
  }
}

std::vector<NodeRecord> Fleet::records() const {
  std::vector<NodeRecord> out;
  for (const auto& n : nodes_) {
    if (!n.adopted) continue;
    NodeRecord r;
    r.id = n.id;
    r.key = n.key;
    r.keyPending = n.keyPending;
    out.push_back(r);
  }
  return out;
}

NodeView* Fleet::findMut(const std::string& id) {
  for (auto& n : nodes_) {
    if (n.id == id) return &n;
  }
  return nullptr;
}

const NodeView* Fleet::find(const std::string& id) const {
  for (const auto& n : nodes_) {
    if (n.id == id) return &n;
  }
  return nullptr;
}

NodeView* Fleet::findMac(const Mac& mac) {
  for (auto& n : nodes_) {
    if (n.mac == mac) return &n;
  }
  return nullptr;
}

NodeView& Fleet::add(const Mac& mac, int64_t nowMs) {
  if (nodes_.size() >= kMaxFleet) {
    // Make room by dropping the waiting node heard from longest ago.
    auto oldest = nodes_.end();
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
      if (!it->adopted && (oldest == nodes_.end() || it->lastSeenMs < oldest->lastSeenMs)) oldest = it;
    }
    if (oldest != nodes_.end()) nodes_.erase(oldest);
  }
  NodeView n;
  n.id = macToId(mac);
  n.mac = mac;
  n.firstSeenMs = nowMs;
  nodes_.push_back(n);
  return nodes_.back();
}

void Fleet::seen(NodeView& n, int64_t nowMs) {
  n.lastSeenMs = nowMs;
  n.online = true;
}

size_t Fleet::welcome(NodeView& n, uint32_t seq, const ReplyContext& ctx, uint8_t* out) {
  WelcomeMsg w;
  w.channel = ctx.channel;
  w.checkInS = ctx.checkInS;
  w.adopted = n.adopted && n.keyPending;
  if (w.adopted) w.key = n.key;
  return encode(w, seq, out);
}

void Fleet::applyAck(NodeView& n, const NodeAck& a) {
  n.ack = a;
  if (!n.idsSynced) {
    // First word from the node since the station started: number the waiting
    // commands after the last one it carried out, so none is mistaken for old.
    n.idsSynced = true;
    uint16_t id = static_cast<uint16_t>(a.lastCmdId + 1);
    for (auto& c : n.queue) {
      c.id = id ? id : 1;
      id = static_cast<uint16_t>(c.id + 1);
    }
    n.nextCmdId = id ? id : 1;
  }
  while (!n.queue.empty() && !newer16(n.queue.front().id, a.lastCmdId)) n.queue.pop_front();
}

size_t Fleet::reply(NodeView& n, uint32_t seq, const ReplyContext& ctx, uint8_t* out) {
  ReplyMsg r;
  r.epoch = ctx.epoch;
  r.utcOffsetS = ctx.utcOffsetS;
  r.env = ctx.env;
  r.automation = ctx.automation;
  r.stop = ctx.stop;

  size_t items = 0;
  if (!n.queue.empty()) {
    r.item = ReplyItem::Command;
    r.command = n.queue.front();
    items = n.queue.size() - 1;
    // The node unpairs on receipt and will never acknowledge it.
    if (r.command.kind == CommandKind::Forget) n.forgetSent = true;
  } else if (n.configPending()) {
    const uint16_t offset = n.ack.cfgRxVersion == n.configVersion ? n.ack.cfgRxOffset : 0;
    r.item = ReplyItem::Config;
    r.chunk.version = n.configVersion;
    r.chunk.total = static_cast<uint16_t>(n.config.size());
    r.chunk.offset = std::min<uint16_t>(offset, r.chunk.total);
    r.chunk.data = n.config.substr(r.chunk.offset, kMaxChunk);
    items = r.chunk.offset + r.chunk.data.size() < n.config.size() ? 1 : 0;
  }
  if (r.item == ReplyItem::Command && n.configPending()) items++;
  r.more = items > 0;
  return encode(r, seq, n.key, n.mac, out);
}

size_t Fleet::onFrame(const Mac& from, const uint8_t* d, size_t len, int64_t nowMs, const ReplyContext& ctx,
                      uint8_t* out) {
  Header h;
  if (!parseHeader(d, len, h)) return 0;
  NodeView* n = findMac(from);

  if (h.type == MsgType::Hello) {
    HelloMsg m;
    if (!decode(d, len, m)) return 0;
    if (!n) n = &add(from, nowMs);
    n->board = m.board;
    n->fw = m.fw;
    seen(*n, nowMs);
    // An added node saying Hello lost its key. It only gets one again when you
    // confirm it in the dashboard (so a copycat cannot ask for it).
    n->wantsRepair = n->adopted && !n->keyPending;
    sink_.onNodeHello(*n);
    return welcome(*n, h.seq, ctx, out);
  }

  if (h.type != MsgType::Report && h.type != MsgType::Event && h.type != MsgType::Poll) return 0;
  if (!n || !n->adopted) {
    RejectMsg r;
    return encode(r, h.seq, out);
  }
  if (!checkTag(d, len, n->key, from)) return 0;  // not from this node: stay silent

  // A repeat (our reply got lost): answer the same again without acting twice.
  if (n->haveSeq && h.seq == n->lastSeq) {
    seen(*n, nowMs);
    std::copy(n->lastReply.begin(), n->lastReply.begin() + n->lastReplyLen, out);
    return n->lastReplyLen;
  }
  if (n->haveSeq && !newer32(h.seq, n->lastSeq)) return 0;  // replayed

  NodeAck ack;
  if (h.type == MsgType::Report) {
    ReportMsg m;
    if (!decode(d, len, m)) return 0;
    ack = m.ack;
    n->report = m;
    n->haveReport = true;
    n->reportMs = nowMs;
    n->fw = m.fw;
  } else if (h.type == MsgType::Event) {
    EventMsg m;
    if (!decode(d, len, m)) return 0;
    ack = m.ack;
    sink_.onNodeEvent(*n, m.event);
  } else {
    PollMsg m;
    if (!decode(d, len, m)) return 0;
    ack = m.ack;
  }

  n->haveSeq = true;
  n->lastSeq = h.seq;
  seen(*n, nowMs);
  n->wantsRepair = false;
  if (n->keyPending) {
    n->keyPending = false;  // the node uses its key: never hand it out again
    sink_.onNodeRecordsChanged();
  }
  applyAck(*n, ack);
  if (h.type == MsgType::Report) sink_.onNodeReport(*n);

  const size_t rl = reply(*n, h.seq, ctx, out);
  n->lastReplyLen = rl;
  std::copy(out, out + rl, n->lastReply.begin());

  if (n->forgetSent) {
    const std::string id = n->id;
    nodes_.erase(nodes_.begin() + (n - nodes_.data()));
    sink_.onNodeRecordsChanged();
    sink_.onNodeForgotten(id);
  }
  return rl;
}

bool Fleet::adopt(const std::string& id, const Key& key) {
  NodeView* n = findMut(id);
  if (!n) {
    Mac mac;
    if (!idToMac(id, mac)) return false;
    n = &add(mac, 0);
  }
  n->adopted = true;
  n->keyPending = true;
  n->wantsRepair = false;
  n->forgetting = n->forgetSent = false;
  n->haveSeq = false;  // a re-paired node starts a fresh conversation
  n->idsSynced = false;
  n->queue.clear();
  n->ack = NodeAck{};
  n->key = key;
  sink_.onNodeRecordsChanged();
  return true;
}

bool Fleet::forget(const std::string& id) {
  NodeView* n = findMut(id);
  if (!n) return false;
  if (!n->adopted || n->keyPending) {
    // It never used a key: nothing to tell it.
    nodes_.erase(nodes_.begin() + (n - nodes_.data()));
    sink_.onNodeRecordsChanged();
    return true;
  }
  n->queue.clear();
  n->config.clear();
  n->configVersion = 0;
  n->forgetting = true;
  Command c;
  c.id = n->nextCmdId;
  n->nextCmdId = static_cast<uint16_t>(n->nextCmdId + 1);
  if (n->nextCmdId == 0) n->nextCmdId = 1;
  c.kind = CommandKind::Forget;
  n->queue.push_back(c);
  return true;
}

void Fleet::setConfig(const std::string& id, const std::string& json) {
  NodeView* n = findMut(id);
  if (!n || n->forgetting) return;
  const uint32_t v = json.empty() ? 0 : fnv1a(json);
  if (v == n->configVersion) return;
  n->config = json;
  n->configVersion = v;
}

uint16_t Fleet::queue(const std::string& id, CommandKind kind, float arg) {
  NodeView* n = findMut(id);
  if (!n || !n->adopted || n->forgetting || n->queue.size() >= kMaxQueuedCommands) return 0;
  Command c;
  c.id = n->nextCmdId;
  n->nextCmdId = static_cast<uint16_t>(n->nextCmdId + 1);
  if (n->nextCmdId == 0) n->nextCmdId = 1;
  c.kind = kind;
  c.arg = arg;
  n->queue.push_back(c);
  return c.id;
}

int64_t Fleet::etaS(const std::string& id, int64_t nowMs, uint16_t checkInS) const {
  const NodeView* n = find(id);
  if (!n || n->lastSeenMs < 0) return -1;
  const int64_t due = n->lastSeenMs + int64_t(checkInS) * 1000;
  return std::max<int64_t>(0, (due - nowMs + 999) / 1000);
}

void Fleet::tick(int64_t nowMs, uint16_t checkInS) {
  const int64_t offlineMs = int64_t(kOfflineAfterCheckIns) * checkInS * 1000 + 60000;
  for (auto& n : nodes_) {
    const int64_t last = n.lastSeenMs >= 0 ? n.lastSeenMs : 0;  // never heard: count from start-up
    n.online = n.lastSeenMs >= 0 && nowMs - last < offlineMs;
  }
  nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                              [&](const NodeView& n) {
                                return !n.adopted && n.lastSeenMs >= 0 && nowMs - n.lastSeenMs > kPendingForgetMs;
                              }),
               nodes_.end());
}

}  // namespace plantlink
