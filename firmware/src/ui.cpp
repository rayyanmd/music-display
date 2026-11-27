#include "ui.h"
#include "lyrics.h"

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
constexpr uint16_t kDimmer = 0x632C;
constexpr uint32_t kLyricsWaitMs = 10000;   // "Looking for lyrics…" before giving up
constexpr uint32_t kSlideMs = 150;
constexpr int kRowSmall = 20;              // row height for 12 px lines
constexpr int kRowBig = 22;                // row height for 16 px current line
constexpr int kPlainVisible = 6;

const char* lineOrNote(const std::string& t) { return t.empty() ? "\u266A" : t.c_str(); }

// Byte length of the UTF-8 sequence starting with lead byte c.
size_t u8len(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }

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

void Ui::toggleView() { view_ = view_ == View::Main ? View::Lyrics : View::Main; }

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
    view_ = View::Main;
    drawIdle(s);
  } else if (view_ == View::Lyrics) {
    drawLyricsView(s, nowMs);
  } else {
    drawStatus(s);
    drawArt(s);
    drawText(s, nowMs);
    drawProgress(s);
    drawButtons(s, nowMs, kPrev, kPlay, kNext);
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

void Ui::drawButtons(const Snapshot& s, uint32_t nowMs, const Rect& rPrev, const Rect& rPlay, const Rect& rNext) {
  bool flashing = (int32_t)(flashUntilMs_ - nowMs) > 0;
  auto bg = [&](const Rect& r, Button b) {
    if (flashing && flash_ == b) frame_.fillRoundRect(r.x, r.y + 2, r.w, r.h - 4, 8, kPress);
  };
  bg(rPrev, Button::Prev);
  bg(rPlay, Button::PlayPause);
  bg(rNext, Button::Next);

  // prev: bar + left triangle
  int cy = rPrev.y + rPrev.h / 2;
  int px = rPrev.x + rPrev.w / 2;
  frame_.fillRect(px - 9, cy - 8, 3, 16, kFg);
  frame_.fillTriangle(px + 8, cy - 8, px + 8, cy + 8, px - 5, cy, kFg);
  // next: right triangle + bar
  int nx = rNext.x + rNext.w / 2;
  frame_.fillTriangle(nx - 8, cy - 8, nx - 8, cy + 8, nx + 5, cy, kFg);
  frame_.fillRect(nx + 6, cy - 8, 3, 16, kFg);
  // play/pause in a green circle
  int cx = rPlay.x + rPlay.w / 2;
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

// ---------------- lyrics screen ----------------

void Ui::drawCentered(const char* msg, const lgfx::IFont* font, uint16_t color) {
  frame_.setFont(font);
  frame_.setTextDatum(lgfx::middle_center);
  frame_.setTextColor(color);
  frame_.drawString(msg, kLyrics.x + kLyrics.w / 2, kLyrics.y + kLyrics.h / 2);
}

std::vector<std::string> Ui::wrapText(const std::string& text, const lgfx::IFont* font, int width, int maxRows) {
  frame_.setFont(font);
  std::vector<std::string> rows;
  size_t start = 0;
  while (start < text.size() && (int)rows.size() < maxRows) {
    if ((int)rows.size() == maxRows - 1) {  // last row takes the rest (clipped when drawn)
      rows.push_back(text.substr(start));
      break;
    }
    size_t i = start, lastSpace = std::string::npos, fit = start;
    while (i < text.size()) {
      size_t n = u8len((unsigned char)text[i]);
      size_t end = std::min(text.size(), i + n);
      if (frame_.textWidth(text.substr(start, end - start).c_str()) > width) break;
      if (text[i] == ' ') lastSpace = i;
      fit = end;
      i = end;
    }
    if (fit >= text.size()) {
      rows.push_back(text.substr(start));
      break;
    }
    size_t cut = (lastSpace != std::string::npos && lastSpace > start) ? lastSpace : fit;
    if (cut == start) cut = start + u8len((unsigned char)text[start]);  // a single glyph wider than the row
    rows.push_back(text.substr(start, cut - start));
    start = cut;
    while (start < text.size() && text[start] == ' ') start++;
  }
  return rows;
}

void Ui::drawLyricsView(const Snapshot& s, uint32_t nowMs) {
  drawStatus(s);
  const auto& ly = s.lyrics;
  if (ly && ly->kind == proto::LyricsKind::Plain) {
    frame_.setFont(kFontBody);
    frame_.setTextDatum(lgfx::middle_right);
    frame_.setTextColor(kDimmer);
    frame_.drawString("unsynced", kScreenW - 14 - s.connected * 10, kStatus.h / 2);
  }

  frame_.setClipRect(kLyrics.x, kLyrics.y, kLyrics.w, kLyrics.h);
  if (!ly) {
    bool waiting = nowMs - trackSinceMs_ < kLyricsWaitMs;
    drawCentered(waiting ? "Looking for lyrics\u2026" : "No lyrics for this song", kFontBody, kDim);
  } else if (ly->kind == proto::LyricsKind::Instrumental) {
    drawCentered("\u266A Instrumental", kFontTitle, kDim);
  } else if (ly->kind == proto::LyricsKind::None || ly->lines.empty()) {
    drawCentered("No lyrics for this song", kFontBody, kDim);
  } else if (ly->kind == proto::LyricsKind::Plain) {
    drawPlain(s);
  } else {
    drawSynced(s, nowMs);
  }
  frame_.clearClipRect();

  // bottom strip: progress, title, times, buttons
  frame_.fillRoundRect(kLProgress.x, kLProgress.y, kLProgress.w, kLProgress.h, 2, kTrack);
  int w = progressWidth(s.positionMs, s.track.durationMs, kLProgress.w);
  if (w > 0) frame_.fillRoundRect(kLProgress.x, kLProgress.y, w, kLProgress.h, 2, s.playing ? kAccent : kDim);
  std::string info = s.track.title.empty() ? std::string("\u2014") : s.track.title;
  if (!s.track.artist.empty()) info += " \u2014 " + s.track.artist;
  marquee(info, kLInfo, kFontBody, kFg, nowMs - trackSinceMs_);
  std::string times = formatTime(s.positionMs) + " / " + formatTime(s.track.durationMs);
  frame_.setFont(kFontBody);
  frame_.setTextDatum(lgfx::top_left);
  frame_.setTextColor(kDim);
  frame_.drawString(times.c_str(), kLTimes.x, kLTimes.y);
  drawButtons(s, nowMs, kLPrev, kLPlay, kLNext);
}

void Ui::drawSynced(const Snapshot& s, uint32_t nowMs) {
  const auto& lines = s.lyrics->lines;
  int cur = lyrics::currentLine(lines, s.positionMs);
  if (s.lyricsVersion != lyricsVersion_) {
    lyricsVersion_ = s.lyricsVersion;
    lyricCur_ = cur;
    lyricChangeMs_ = nowMs - kSlideMs;  // no slide when lyrics first appear
  } else if (cur != lyricCur_) {
    // Slide only for the normal one-line advance; jumps (seeks) snap.
    lyricChangeMs_ = (cur == lyricCur_ + 1) ? nowMs : nowMs - kSlideMs;
    lyricCur_ = cur;
  }
  uint32_t since = nowMs - lyricChangeMs_;
  int slide = since >= kSlideMs ? 0 : (int)((kSlideMs - since) * kRowBig / kSlideMs);

  if (wrapIdx_ != cur || wrapVersion_ != s.lyricsVersion) {
    wrapIdx_ = cur;
    wrapVersion_ = s.lyricsVersion;
    wrap_ = wrapText(cur < 0 ? std::string("\u266A") : std::string(lineOrNote(lines[cur].text)), kFontTitle,
                     kLyrics.w, 2);
  }

  const int x = kLyrics.x;
  int y = kLyrics.y + 8 + slide;
  frame_.setTextDatum(lgfx::top_left);
  // previous line
  frame_.setFont(kFontBody);
  frame_.setTextColor(kDimmer);
  if (cur >= 1) frame_.drawString(lineOrNote(lines[cur - 1].text), x, y);
  y += kRowSmall + 4;
  // current line (up to 2 rows)
  frame_.setFont(kFontTitle);
  frame_.setTextColor(kFg);
  for (const std::string& row : wrap_) {
    frame_.drawString(row.c_str(), x, y);
    y += kRowBig;
  }
  y += 4;
  // next lines
  frame_.setFont(kFontBody);
  for (int k = 1; k <= 3; k++) {
    int i = cur + k;
    if (i < 0 || i >= (int)lines.size()) break;
    frame_.setTextColor(k == 1 ? kDim : kDimmer);
    frame_.drawString(lineOrNote(lines[i].text), x, y);
    y += kRowSmall;
  }
}

void Ui::drawPlain(const Snapshot& s) {
  const auto& lines = s.lyrics->lines;
  int top = lyrics::plainTopLine(lines.size(), s.positionMs, s.track.durationMs, kPlainVisible);
  frame_.setFont(kFontBody);
  frame_.setTextDatum(lgfx::top_left);
  frame_.setTextColor(kDim);
  int y = kLyrics.y + 6;
  for (int i = top; i < (int)lines.size() && i < top + kPlainVisible + 1; i++) {
    frame_.drawString(lineOrNote(lines[i].text), kLyrics.x, y);
    y += kRowSmall + 2;
  }
}
