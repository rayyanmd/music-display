#include "now_playing.h"

int NowPlaying::find(uint16_t conn) const {
  for (int i = 0; i < kSlots; i++)
    if (slots_[i].conn == conn) return i;
  return -1;
}

int NowPlaying::findOrAlloc(uint16_t conn) {
  int i = find(conn);
  if (i >= 0) return i;
  for (i = 0; i < kSlots; i++) {
    if (slots_[i].conn == kFree) {
      slots_[i] = Slot{};
      slots_[i].conn = conn;
      return i;
    }
  }
  return -1;
}

void NowPlaying::onConnect(uint16_t conn) { findOrAlloc(conn); }

void NowPlaying::onDisconnect(uint16_t conn) {
  int i = find(conn);
  if (i < 0) return;
  slots_[i] = Slot{};
  if (active_ != i) return;
  // Fall back to the most recently updated remaining slot with state.
  active_ = -1;
  uint32_t best = 0;
  for (int j = 0; j < kSlots; j++) {
    const Slot& s = slots_[j];
    if (s.conn == kFree || !s.hasState) continue;
    if (active_ < 0 || s.receivedAtMs >= best) {
      active_ = j;
      best = s.receivedAtMs;
    }
  }
}

void NowPlaying::switchTo(int idx, uint32_t nowMs) {
  if (active_ == idx) return;
  active_ = idx;
  lastSwitchMs_ = nowMs;
}

void NowPlaying::onState(uint16_t conn, const proto::State& st, uint32_t nowMs) {
  int i = findOrAlloc(conn);
  if (i < 0) return;
  Slot& s = slots_[i];
  if (!s.hasState || s.state.trackId != st.trackId) {
    // Track changed: promote what was sent ahead for it (Meta and Art precede State).
    if (s.pendingMeta.trackId == st.trackId && s.meta.trackId != st.trackId) s.meta = s.pendingMeta;
    if (s.pendingArt && s.pendingArtId == st.trackId) {
      s.art = s.pendingArt;
      s.artId = s.pendingArtId;
      s.artSeq = s.pendingArtSeq;
      s.pendingArt.reset();
    }
  }
  s.state = st;
  s.hasState = true;
  s.receivedAtMs = nowMs;

  if (active_ < 0) {
    switchTo(i, nowMs);
    return;
  }
  if (active_ == i || !st.playing) return;

  const Slot& a = slots_[active_];
  bool activePlaying = a.hasState && a.state.playing;
  if (a.hasState && a.state.trackId == st.trackId) {
    // Spotify Connect duplicate (or the same song resumed elsewhere).
    if (a.state.local && !st.local) return;  // a remote controller never displaces the local player
    if (!a.state.local && st.local) {
      if (!activePlaying || nowMs - lastSwitchMs_ >= kSameTrackHoldMs) switchTo(i, nowMs);
      return;
    }
    if (!activePlaying) switchTo(i, nowMs);  // same locality: resumed on the other device
    return;
  }
  switchTo(i, nowMs);
}

void NowPlaying::onMeta(uint16_t conn, const proto::Meta& m) {
  int i = findOrAlloc(conn);
  if (i < 0) return;
  Slot& s = slots_[i];
  if (s.hasState && m.trackId == s.state.trackId) s.meta = m;
  else s.pendingMeta = m;
}

void NowPlaying::onArt(uint16_t conn, const uint8_t* d, size_t n) {
  int i = findOrAlloc(conn);
  if (i < 0) return;
  Slot& s = slots_[i];
  if (s.assembler.feed(d, n) != proto::ArtAssembler::Result::Complete) return;
  uint32_t id = s.assembler.trackId();
  auto img = std::make_shared<const std::vector<uint8_t>>(s.assembler.data());
  s.assembler.reset();
  if (s.hasState && id == s.state.trackId) {
    s.art = std::move(img);
    s.artId = id;
    s.artSeq = ++artCounter_;
  } else {
    s.pendingArt = std::move(img);
    s.pendingArtId = id;
    s.pendingArtSeq = ++artCounter_;
  }
}

int NowPlaying::activeConn() const { return active_ < 0 ? -1 : slots_[active_].conn; }

Snapshot NowPlaying::snapshot(uint32_t nowMs) const {
  Snapshot out;
  for (const Slot& s : slots_)
    if (s.conn != kFree) out.connected++;
  if (active_ < 0) return out;
  const Slot& s = slots_[active_];
  if (!s.hasState) return out;

  out.hasActive = true;
  out.source = s.state.source;
  out.playing = s.state.playing;
  out.track.id = s.state.trackId;
  out.track.durationMs = s.state.durationMs;
  if (s.meta.trackId == s.state.trackId) {
    out.track.title = s.meta.title;
    out.track.artist = s.meta.artist;
    out.track.album = s.meta.album;
  }
  uint64_t pos = s.state.positionMs;
  // A State stamped after nowMs (race between loop and BLE task) counts as zero elapsed.
  if (s.state.playing && (int32_t)(nowMs - s.receivedAtMs) > 0) pos += (uint32_t)(nowMs - s.receivedAtMs);
  if (s.state.durationMs > 0 && pos > s.state.durationMs) pos = s.state.durationMs;
  out.positionMs = (uint32_t)pos;
  if (s.art && s.artId == s.state.trackId) {
    out.art = s.art;
    out.artVersion = s.artSeq;
  }
  return out;
}
