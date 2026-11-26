// Now-playing BLE protocol v1 decoding (see protocol/PROTOCOL.md). Hardware-free.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace proto {

enum class Source : uint8_t { Unknown = 0, Mac = 1, Phone = 2 };
enum class Cmd : uint8_t { PlayPause = 1, Next = 2, Prev = 3 };

struct State {
  uint32_t trackId = 0;
  uint32_t positionMs = 0;
  uint32_t durationMs = 0;
  bool playing = false;
  bool local = false;
  Source source = Source::Unknown;
};

struct Meta {
  uint32_t trackId = 0;
  std::string title, artist, album;
};

constexpr size_t kMaxArt = 32768;
constexpr size_t kMaxField = 160;  // bytes; truncated on a UTF-8 boundary
constexpr size_t kMaxMeta = 512;

// False if n != 16 or ver != 1.
bool decodeState(const uint8_t* d, size_t n, State& out);
// False if n < 4 or n > 512. Missing fields become "".
bool decodeMeta(const uint8_t* d, size_t n, Meta& out);

class ArtAssembler {
 public:
  enum class Result { Ignored, InProgress, Complete, Error };
  // Header (0x01) starts a transfer; data (0x02) must match trackId and continue at the next offset.
  Result feed(const uint8_t* d, size_t n);
  uint32_t trackId() const { return trackId_; }
  const std::vector<uint8_t>& data() const { return buf_; }
  void reset();

 private:
  bool active_ = false;
  uint32_t trackId_ = 0;
  uint32_t total_ = 0;
  std::vector<uint8_t> buf_;
};

}  // namespace proto
