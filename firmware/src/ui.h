// LovyanGFX rendering of a NowPlaying Snapshot (320x240 landscape).
#pragma once
#include "LGFX_ES3C28P.hpp"
#include "layout.h"
#include "now_playing.h"
#include <string>
#include <vector>

class Ui {
 public:
  void begin(LGFX& tft);
  // Redraws at most every kFrameMs; call every loop.
  void render(const Snapshot& s, uint32_t nowMs);
  void flashButton(layout::Button b);
  void toggleView();
  layout::View view() const { return view_; }

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
  layout::View view_ = layout::View::Main;
  // lyrics screen
  uint32_t lyricsVersion_ = 0;
  int lyricCur_ = -2;            // -2 = not yet drawn for these lyrics
  uint32_t lyricChangeMs_ = 0;
  int wrapIdx_ = -2;
  uint32_t wrapVersion_ = 0;
  std::vector<std::string> wrap_;  // current line wrapped to <= 2 rows

  void drawStatus(const Snapshot& s);
  void drawArt(const Snapshot& s);
  void drawText(const Snapshot& s, uint32_t nowMs);
  void drawProgress(const Snapshot& s);
  void drawButtons(const Snapshot& s, uint32_t nowMs, const Rect& prev, const Rect& play, const Rect& next);
  void drawLyricsView(const Snapshot& s, uint32_t nowMs);
  void drawSynced(const Snapshot& s, uint32_t nowMs);
  void drawPlain(const Snapshot& s);
  void drawCentered(const char* msg, const lgfx::IFont* font, uint16_t color);
  std::vector<std::string> wrapText(const std::string& text, const lgfx::IFont* font, int width, int maxRows);
  void drawIdle(const Snapshot& s);
  void marquee(const std::string& text, const Rect& r, const lgfx::IFont* font, uint16_t color, uint32_t elapsedMs);
};
