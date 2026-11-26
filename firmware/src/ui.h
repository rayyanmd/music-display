// LovyanGFX rendering of a NowPlaying Snapshot (320x240 landscape).
#pragma once
#include "LGFX_ES3C28P.hpp"
#include "layout.h"
#include "now_playing.h"

class Ui {
 public:
  void begin(LGFX& tft);
  // Redraws at most every kFrameMs; call every loop.
  void render(const Snapshot& s, uint32_t nowMs);
  void flashButton(layout::Button b);

 private:
  static constexpr uint32_t kFrameMs = 33;
  LGFX* tft_ = nullptr;
  LGFX_Sprite frame_;
  LGFX_Sprite art_;
  uint32_t artVersion_ = 0;
  bool artValid_ = false;
  uint32_t lastFrameMs_ = 0;
  uint32_t trackId_ = 0;
  uint32_t trackSinceMs_ = 0;
  layout::Button flash_ = layout::Button::None;
  uint32_t flashUntilMs_ = 0;
  bool flashPending_ = false;

  void drawStatus(const Snapshot& s);
  void drawArt(const Snapshot& s);
  void drawText(const Snapshot& s, uint32_t nowMs);
  void drawProgress(const Snapshot& s);
  void drawButtons(const Snapshot& s, uint32_t nowMs);
  void drawIdle(const Snapshot& s);
  void marquee(const std::string& text, const Rect& r, const lgfx::IFont* font, uint16_t color, uint32_t elapsedMs);
};
