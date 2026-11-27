// Screen geometry (320x240 landscape), hit-testing and text helpers. Hardware-free.
#pragma once
#include <cstdint>
#include <string>

struct Rect {
  int16_t x, y, w, h;
  constexpr bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

namespace layout {
constexpr int kScreenW = 320, kScreenH = 240;
constexpr Rect kStatus{0, 0, 320, 20};
constexpr Rect kArt{8, 24, 160, 160};
constexpr Rect kTitle{178, 28, 134, 20};
constexpr Rect kArtist{178, 52, 134, 16};
constexpr Rect kAlbum{178, 70, 134, 16};
constexpr Rect kProgress{178, 140, 134, 6};
constexpr Rect kTimes{178, 150, 134, 14};
constexpr Rect kPrev{178, 176, 40, 48};
constexpr Rect kPlay{225, 176, 40, 48};
constexpr Rect kNext{272, 176, 40, 48};
constexpr int kTouchSlop = 6;

// Lyrics view
constexpr Rect kLyrics{8, 24, 304, 138};
constexpr Rect kLProgress{8, 168, 304, 4};
constexpr Rect kLInfo{8, 178, 160, 14};
constexpr Rect kLTimes{8, 196, 160, 14};
constexpr Rect kLPrev{178, 190, 40, 36};
constexpr Rect kLPlay{225, 190, 40, 36};
constexpr Rect kLNext{272, 190, 40, 36};

enum class Button { None, Prev, PlayPause, Next };
enum class View { Main, Lyrics };
enum class Action { None, Prev, PlayPause, Next, ToggleView };

// Buttons of the view first (exact, then nearest within kTouchSlop); then the view's toggle area
// (kArt on Main, kLyrics on Lyrics).
Action hitTest(View v, int x, int y);
Button toButton(Action a);              // None for ToggleView/None
std::string formatTime(uint32_t ms);    // "m:ss", or "h:mm:ss" at 1 h or more
int progressWidth(uint32_t pos, uint32_t dur, int fullW);  // 0 if dur == 0, clamped to fullW
}  // namespace layout
