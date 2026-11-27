#include "lyrics.h"
#include <algorithm>

namespace lyrics {

int currentLine(const std::vector<proto::LyricLine>& l, uint32_t posMs) {
  auto it = std::upper_bound(l.begin(), l.end(), posMs,
                             [](uint32_t p, const proto::LyricLine& x) { return p < x.timeMs; });
  return (int)(it - l.begin()) - 1;
}

int plainTopLine(size_t count, uint32_t posMs, uint32_t durMs, int visible) {
  int maxTop = std::max(0, (int)count - visible);
  if (durMs == 0 || maxTop == 0) return 0;
  uint64_t steps = (uint64_t)(maxTop + 1);
  int top = (int)((uint64_t)std::min(posMs, durMs) * steps / durMs);
  return std::min(std::max(top, 0), maxTop);
}

}  // namespace lyrics
