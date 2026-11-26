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
  if (activePlaying && a.state.trackId == st.trackId) {
    // Spotify Connect duplicate: only move from a remote-controlling source to the local one, and not too often.
    if (!a.state.local && st.local && nowMs - lastSwitchMs_ >= kSameTrackHoldMs) switchTo(i, nowMs);
    return;
  }
  switchTo(i, nowMs);
}

void NowPlaying::onMeta(uint16_t conn, const proto::Meta& m) {
  int i = findOrAlloc(conn);
  if (i >= 0) slots_[i].meta = m;
}

void NowPlaying::onArt(uint16_t conn, const uint8_t* d, size_t n) {
  int i = findOrAlloc(conn);
  if (i < 0) return;
  Slot& s = slots_[i];
  if (s.assembler.feed(d, n) == proto::ArtAssembler::Result::Complete) {
    s.artId = s.assembler.trackId();
    s.art = s.assembler.data();
    s.assembler.reset();
    s.artSeq = ++artCounter_;
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
  if (s.state.playing) pos += (uint32_t)(nowMs - s.receivedAtMs);
  if (s.state.durationMs > 0 && pos > s.state.durationMs) pos = s.state.durationMs;
  out.positionMs = (uint32_t)pos;
  if (!s.art.empty() && s.artId == s.state.trackId) {
    out.art = &s.art;
    out.artVersion = s.artSeq;
  }
  return out;
}
