#include "protocol.h"

namespace proto {
namespace {

uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Copy [b, e) truncated to kMaxField bytes without splitting a UTF-8 sequence.
std::string field(const uint8_t* b, const uint8_t* e) {
  size_t n = (size_t)(e - b);
  if (n > kMaxField) {
    n = kMaxField;
    // Back up over continuation bytes, then drop the lead byte of the cut sequence.
    while (n > 0 && (b[n] & 0xC0) == 0x80) n--;
  }
  return std::string(reinterpret_cast<const char*>(b), n);
}

}  // namespace

bool decodeState(const uint8_t* d, size_t n, State& out) {
  if (d == nullptr || n != 16 || d[0] != 1) return false;
  out.playing = (d[1] & 0x01) != 0;
  out.local = (d[1] & 0x02) != 0;
  out.source = d[2] <= 2 ? static_cast<Source>(d[2]) : Source::Unknown;
  out.trackId = rd32(d + 4);
  out.positionMs = rd32(d + 8);
  out.durationMs = rd32(d + 12);
  return true;
}

bool decodeMeta(const uint8_t* d, size_t n, Meta& out) {
  if (d == nullptr || n < 4 || n > kMaxMeta) return false;
  out = Meta{};
  out.trackId = rd32(d);
  std::string* fields[3] = {&out.title, &out.artist, &out.album};
  const uint8_t* p = d + 4;
  const uint8_t* end = d + n;
  for (int i = 0; i < 3 && p <= end; i++) {
    const uint8_t* q = p;
    while (q < end && *q != 0x1F) q++;
    *fields[i] = field(p, q);
    if (q >= end) break;
    p = q + 1;
  }
  return true;
}

void ArtAssembler::reset() {
  active_ = false;
  total_ = 0;
  buf_.clear();
}

ArtAssembler::Result ArtAssembler::feed(const uint8_t* d, size_t n) {
  if (d == nullptr || n < 1) return Result::Ignored;
  if (d[0] == 0x01) {
    if (n != 9) return Result::Ignored;
    reset();
    trackId_ = rd32(d + 1);
    uint32_t len = rd32(d + 5);
    if (len == 0 || len > kMaxArt) return Result::Error;
    total_ = len;
    buf_.reserve(len);
    active_ = true;
    return Result::InProgress;
  }
  if (d[0] != 0x02 || n < 9 || !active_) return Result::Ignored;
  uint32_t id = rd32(d + 1);
  uint32_t off = rd32(d + 5);
  size_t len = n - 9;
  if (id != trackId_ || off != buf_.size() || off + len > total_) {
    reset();
    return Result::Error;
  }
  buf_.insert(buf_.end(), d + 9, d + n);
  if (buf_.size() == total_) {
    active_ = false;
    return Result::Complete;
  }
  return Result::InProgress;
}

bool decodeLyricsBody(const uint8_t* d, size_t n, std::vector<LyricLine>& out) {
  out.clear();
  if (n > kMaxLyrics || (d == nullptr && n > 0)) return false;
  size_t i = 0;
  uint32_t last = 0;
  while (i < n) {
    if (n - i < 5) return false;
    uint32_t t = rd32(d + i);
    size_t len = d[i + 4];
    i += 5;
    if (len > kMaxLyricLine || len > n - i || t < last) return false;
    out.push_back(LyricLine{t, std::string(reinterpret_cast<const char*>(d + i), len)});
    last = t;
    i += len;
  }
  return true;
}

void LyricsAssembler::reset() {
  active_ = false;
  total_ = 0;
  buf_.clear();
}

LyricsAssembler::Result LyricsAssembler::feed(const uint8_t* d, size_t n) {
  if (d == nullptr || n < 1) return Result::Ignored;
  if (d[0] == 0x01) {
    reset();
    if (n != 10) return Result::Error;
    trackId_ = rd32(d + 1);
    uint32_t len = rd32(d + 5);
    uint8_t k = d[9];
    if (k > 3) return Result::Error;
    kind_ = static_cast<LyricsKind>(k);
    bool bodyless = kind_ == LyricsKind::None || kind_ == LyricsKind::Instrumental;
    if (bodyless) return len == 0 ? Result::Complete : Result::Error;
    if (len == 0 || len > kMaxLyrics) return Result::Error;
    total_ = len;
    buf_.reserve(len);
    active_ = true;
    return Result::InProgress;
  }
  if (d[0] != 0x02 || n < 9 || !active_) return Result::Ignored;
  uint32_t id = rd32(d + 1);
  uint32_t off = rd32(d + 5);
  size_t len = n - 9;
  if (id != trackId_ || off != buf_.size() || len > total_ - off) {
    reset();
    return Result::Error;
  }
  buf_.insert(buf_.end(), d + 9, d + n);
  if (buf_.size() == total_) {
    active_ = false;
    return Result::Complete;
  }
  return Result::InProgress;
}

}  // namespace proto
