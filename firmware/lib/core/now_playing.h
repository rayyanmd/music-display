// Per-source now-playing state and arbitration. Not thread-safe: callers hold a lock.
#pragma once
#include <cstdint>
#include <memory>
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
  // Complete JPEG for track.id, else null. Shared ownership: stays valid after the model changes.
  std::shared_ptr<const std::vector<uint8_t>> art;
  uint32_t artVersion = 0;                     // changes whenever the shown art changes (0 = none)
  // Lyrics for track.id (any kind, including None), else null when nothing was received yet.
  std::shared_ptr<const proto::Lyrics> lyrics;
  uint32_t lyricsVersion = 0;                  // changes whenever the shown lyrics change (0 = none)
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
  void onLyrics(uint16_t conn, const uint8_t* d, size_t n);
  Snapshot snapshot(uint32_t nowMs) const;
  int activeConn() const;  // conn handle commands go to, -1 if none

 private:
  static constexpr uint16_t kFree = 0xFFFF;
  struct Slot {
    uint16_t conn = kFree;
    bool hasState = false;
    proto::State state;
    proto::Meta meta;         // for state.trackId (shown)
    proto::Meta pendingMeta;  // for an upcoming track, promoted when its State arrives
    proto::ArtAssembler assembler;
    uint32_t artId = 0;
    std::shared_ptr<const std::vector<uint8_t>> art;  // shown image
    uint32_t artSeq = 0;
    uint32_t pendingArtId = 0;
    std::shared_ptr<const std::vector<uint8_t>> pendingArt;
    uint32_t pendingArtSeq = 0;
    proto::LyricsAssembler lyricsAsm;
    std::shared_ptr<const proto::Lyrics> lyrics;         // shown (may belong to an older track)
    uint32_t lyricsSeq = 0;
    std::shared_ptr<const proto::Lyrics> pendingLyrics;  // for an upcoming track
    uint32_t pendingLyricsSeq = 0;
    uint32_t receivedAtMs = 0;
  };
  Slot slots_[kSlots];
  int active_ = -1;  // slot index
  uint32_t lastSwitchMs_ = 0;
  uint32_t artCounter_ = 0;
  uint32_t lyricsCounter_ = 0;

  int find(uint16_t conn) const;
  int findOrAlloc(uint16_t conn);
  void switchTo(int idx, uint32_t nowMs);
};
