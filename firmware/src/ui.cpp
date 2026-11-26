#include "ui.h"

using namespace layout;

namespace {
constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kDim = 0xAD55;
constexpr uint16_t kAccent = 0x1DCA;   // Spotify green #1DB954
constexpr uint16_t kTrack = 0x31A6;    // progress track / placeholder grey
constexpr uint16_t kPress = 0x2124;
const lgfx::IFont* kFontTitle = &fonts::efontJA_16;
const lgfx::IFont* kFontBody = &fonts::efontJA_12;
constexpr float kMarqueePxPerS = 30.0f;
constexpr uint32_t kMarqueePauseMs = 1000;
constexpr uint32_t kFlashMs = 150;

const char* sourceLabel(proto::Source s) {
  switch (s) {
    case proto::Source::Mac: return "Mac";
    case proto::Source::Phone: return "Phone";
    default: return "Device";
  }
}
}  // namespace

void Ui::begin(LGFX& tft) {
  tft_ = &tft;
  frame_.setColorDepth(16);
  frame_.setPsram(true);
  frame_.createSprite(kScreenW, kScreenH);
  art_.setColorDepth(16);
  art_.setPsram(true);
  art_.createSprite(kArt.w, kArt.h);
  frame_.fillScreen(kBg);
  frame_.pushSprite(tft_, 0, 0);
}

void Ui::flashButton(Button b) {
  flash_ = b;
  flashPending_ = true;
}

void Ui::render(const Snapshot& s, uint32_t nowMs) {
  if (nowMs - lastFrameMs_ < kFrameMs) return;
  lastFrameMs_ = nowMs;
  if (flashPending_) {
    flashUntilMs_ = nowMs + kFlashMs;
    flashPending_ = false;
  }
  if (s.track.id != trackId_) {
    trackId_ = s.track.id;
    trackSinceMs_ = nowMs;
  }

  frame_.fillScreen(kBg);
  if (!s.hasActive) {
    drawIdle(s);
  } else {
    drawStatus(s);
    drawArt(s);
    drawText(s, nowMs);
    drawProgress(s);
    drawButtons(s, nowMs);
  }
  frame_.pushSprite(tft_, 0, 0);
}

void Ui::drawStatus(const Snapshot& s) {
  frame_.setFont(kFontBody);
  frame_.setTextDatum(lgfx::middle_left);
  frame_.fillCircle(12, kStatus.h / 2, 4, kAccent);
  frame_.setTextColor(kAccent, kBg);
  frame_.drawString(sourceLabel(s.source), 22, kStatus.h / 2);
  for (int i = 0; i < s.connected; i++) frame_.fillCircle(kScreenW - 10 - i * 10, kStatus.h / 2, 3, kDim);
}

void Ui::drawArt(const Snapshot& s) {
  if (s.art == nullptr) {
    artValid_ = false;
    frame_.fillRoundRect(kArt.x, kArt.y, kArt.w, kArt.h, 8, kTrack);
    frame_.setFont(kFontTitle);
    frame_.setTextSize(3);
    frame_.setTextDatum(lgfx::middle_center);
    frame_.setTextColor(kDim, kTrack);
    frame_.drawString("\u266A", kArt.x + kArt.w / 2, kArt.y + kArt.h / 2);
    frame_.setTextSize(1);
    return;
  }
  if (!artValid_ || artVersion_ != s.artVersion) {
    art_.fillScreen(kTrack);
    // scale 0 = fit into kArt.w x kArt.h
    art_.drawJpg(s.art->data(), s.art->size(), 0, 0, kArt.w, kArt.h, 0, 0, 0.0f, 0.0f, lgfx::middle_center);
    artVersion_ = s.artVersion;
    artValid_ = true;
  }
  art_.pushSprite(&frame_, kArt.x, kArt.y);
}

void Ui::marquee(const std::string& text, const Rect& r, const lgfx::IFont* font, uint16_t color, uint32_t elapsedMs) {
  frame_.setFont(font);
  frame_.setTextDatum(lgfx::top_left);
  frame_.setTextColor(color);
  int tw = frame_.textWidth(text.c_str());
  int off = 0;
  if (tw > r.w) {
    int travel = tw - r.w;
    uint32_t scrollMs = (uint32_t)(travel * 1000 / kMarqueePxPerS);
    uint32_t cycle = kMarqueePauseMs + scrollMs + kMarqueePauseMs;
    uint32_t t = elapsedMs % cycle;
    if (t <= kMarqueePauseMs) off = 0;
    else if (t <= kMarqueePauseMs + scrollMs) off = (int)((t - kMarqueePauseMs) * kMarqueePxPerS / 1000);
    else off = travel;
  }
  frame_.setClipRect(r.x, r.y, r.w, r.h);
  frame_.drawString(text.c_str(), r.x - off, r.y);
  frame_.clearClipRect();
}

void Ui::drawText(const Snapshot& s, uint32_t nowMs) {
  uint32_t el = nowMs - trackSinceMs_;
  marquee(s.track.title.empty() ? std::string("—") : s.track.title, kTitle, kFontTitle, kFg, el);
  marquee(s.track.artist, kArtist, kFontBody, kDim, el);
  marquee(s.track.album, kAlbum, kFontBody, kDim, el);
}

void Ui::drawProgress(const Snapshot& s) {
  frame_.fillRoundRect(kProgress.x, kProgress.y, kProgress.w, kProgress.h, 3, kTrack);
  int w = progressWidth(s.positionMs, s.track.durationMs, kProgress.w);
  if (w > 0) frame_.fillRoundRect(kProgress.x, kProgress.y, w, kProgress.h, 3, s.playing ? kAccent : kDim);
  frame_.setFont(kFontBody);
  frame_.setTextColor(kDim);
  frame_.setTextDatum(lgfx::top_left);
  frame_.drawString(formatTime(s.positionMs).c_str(), kTimes.x, kTimes.y);
  frame_.setTextDatum(lgfx::top_right);
  frame_.drawString(formatTime(s.track.durationMs).c_str(), kTimes.x + kTimes.w, kTimes.y);
}

void Ui::drawButtons(const Snapshot& s, uint32_t nowMs) {
  bool flashing = (int32_t)(flashUntilMs_ - nowMs) > 0;
  auto bg = [&](const Rect& r, Button b) {
    if (flashing && flash_ == b) frame_.fillRoundRect(r.x, r.y + 4, r.w, r.h - 8, 8, kPress);
  };
  bg(kPrev, Button::Prev);
  bg(kPlay, Button::PlayPause);
  bg(kNext, Button::Next);

  // prev: bar + left triangle
  int cy = kPrev.y + kPrev.h / 2;
  int px = kPrev.x + kPrev.w / 2;
  frame_.fillRect(px - 9, cy - 8, 3, 16, kFg);
  frame_.fillTriangle(px + 8, cy - 8, px + 8, cy + 8, px - 5, cy, kFg);
  // next: right triangle + bar
  int nx = kNext.x + kNext.w / 2;
  frame_.fillTriangle(nx - 8, cy - 8, nx - 8, cy + 8, nx + 5, cy, kFg);
  frame_.fillRect(nx + 6, cy - 8, 3, 16, kFg);
  // play/pause in a green circle
  int cx = kPlay.x + kPlay.w / 2;
  frame_.fillCircle(cx, cy, 17, kAccent);
  if (s.playing) {
    frame_.fillRect(cx - 6, cy - 7, 4, 14, kBg);
    frame_.fillRect(cx + 2, cy - 7, 4, 14, kBg);
  } else {
    frame_.fillTriangle(cx - 4, cy - 8, cx - 4, cy + 8, cx + 8, cy, kBg);
  }
}

void Ui::drawIdle(const Snapshot& s) {
  frame_.setTextDatum(lgfx::middle_center);
  frame_.setFont(kFontTitle);
  frame_.setTextColor(kFg);
  frame_.drawString("Nothing playing", kScreenW / 2, kScreenH / 2 - 12);
  frame_.setFont(kFontBody);
  frame_.setTextColor(kDim);
  frame_.drawString(s.connected ? "Connected - start Spotify" : "Waiting for Mac or phone", kScreenW / 2, kScreenH / 2 + 14);
}
