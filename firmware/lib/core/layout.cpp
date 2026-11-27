#include "layout.h"
#include <cstdio>

namespace layout {
namespace {
struct Entry {
  Rect r;
  Action a;
};
constexpr Entry kMainButtons[] = {{kPrev, Action::Prev}, {kPlay, Action::PlayPause}, {kNext, Action::Next}};
constexpr Entry kLyricsButtons[] = {{kLPrev, Action::Prev}, {kLPlay, Action::PlayPause}, {kLNext, Action::Next}};

// Distance from point to rect (0 inside), Chebyshev metric.
int dist(const Rect& r, int x, int y) {
  int dx = x < r.x ? r.x - x : (x >= r.x + r.w ? x - (r.x + r.w - 1) : 0);
  int dy = y < r.y ? r.y - y : (y >= r.y + r.h ? y - (r.y + r.h - 1) : 0);
  return dx > dy ? dx : dy;
}
}  // namespace

Action hitTest(View v, int x, int y) {
  const Entry* buttons = v == View::Main ? kMainButtons : kLyricsButtons;
  Action best = Action::None;
  int bestD = kTouchSlop + 1;
  for (int i = 0; i < 3; i++) {
    int d = dist(buttons[i].r, x, y);
    if (d < bestD) {
      bestD = d;
      best = buttons[i].a;
    }
  }
  if (best != Action::None) return best;
  const Rect& toggle = v == View::Main ? kArt : kLyrics;
  return toggle.contains(x, y) ? Action::ToggleView : Action::None;
}

Button toButton(Action a) {
  switch (a) {
    case Action::Prev: return Button::Prev;
    case Action::PlayPause: return Button::PlayPause;
    case Action::Next: return Button::Next;
    default: return Button::None;
  }
}

std::string formatTime(uint32_t ms) {
  uint32_t t = ms / 1000;
  char buf[16];
  if (t >= 3600)
    snprintf(buf, sizeof buf, "%u:%02u:%02u", (unsigned)(t / 3600), (unsigned)(t / 60 % 60), (unsigned)(t % 60));
  else
    snprintf(buf, sizeof buf, "%u:%02u", (unsigned)(t / 60), (unsigned)(t % 60));
  return buf;
}

int progressWidth(uint32_t pos, uint32_t dur, int fullW) {
  if (dur == 0 || fullW <= 0) return 0;
  if (pos >= dur) return fullW;
  return (int)((uint64_t)pos * (uint64_t)fullW / dur);
}
}  // namespace layout
