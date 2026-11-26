// Per-source now-playing state and arbitration. Not thread-safe: callers hold a lock.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "protocol.h"

struct Track {
  uint32_t id = 0;
  std::string title, artist, album;
  uint32_t durationMs = 0;
};

struct Snapshot {
  bool hasActive = false;
  proto::Source source = proto::Source::Unknown;
  Track track;
  bool playing = false;
  uint32_t positionMs = 0;                     // interpolated, clamped to duration
  const std::vector<uint8_t>* art = nullptr;   // complete JPEG for track.id, else nullptr
  uint32_t artVersion = 0;                     // changes whenever the shown art changes (0 = none)
  uint8_t connected = 0;
};

class NowPlaying {
 public:
  static constexpr int kSlots = 3;
  static constexpr uint32_t kSameTrackHoldMs = 3000;

  void onConnect(uint16_t conn);
  void onDisconnect(uint16_t conn);
  void onState(uint16_t conn, const proto::State& s, uint32_t nowMs);
  void onMeta(uint16_t conn, const proto::Meta& m);
  void onArt(uint16_t conn, const uint8_t* d, size_t n);
  Snapshot snapshot(uint32_t nowMs) const;
  int activeConn() const;  // conn handle commands go to, -1 if none

 private:
  static constexpr uint16_t kFree = 0xFFFF;
  struct Slot {
    uint16_t conn = kFree;
    bool hasState = false;
    proto::State state;
    proto::Meta meta;
    proto::ArtAssembler assembler;
    uint32_t artId = 0;
    std::vector<uint8_t> art;  // last complete image
    uint32_t artSeq = 0;
    uint32_t receivedAtMs = 0;
  };
  Slot slots_[kSlots];
  int active_ = -1;  // slot index
  uint32_t lastSwitchMs_ = 0;
  uint32_t artCounter_ = 0;

  int find(uint16_t conn) const;
  int findOrAlloc(uint16_t conn);
  void switchTo(int idx, uint32_t nowMs);
};
