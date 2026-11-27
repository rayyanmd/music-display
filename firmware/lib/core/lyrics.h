// Lyric line selection for the lyrics screen. Hardware-free.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "protocol.h"

namespace lyrics {
// Index of the last line with timeMs <= posMs; -1 before the first line or when empty.
int currentLine(const std::vector<proto::LyricLine>& l, uint32_t posMs);
// First visible line for untimed lyrics, scrolled in proportion to pos/dur; 0 when dur == 0.
int plainTopLine(size_t count, uint32_t posMs, uint32_t durMs, int visible);
}  // namespace lyrics
