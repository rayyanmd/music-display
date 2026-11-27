// Now-playing display firmware for the LCDwiki ES3C28P (ESP32-S3, 2.8" 320x240).
#include <Arduino.h>
#include <mutex>
#include "LGFX_ES3C28P.hpp"
#include "layout.h"
#include "now_playing.h"
#include "tap_detector.h"
#include "ble_service.h"
#include "ui.h"
#if DEMO
#include "demo_art.h"
#endif

static constexpr int PIN_AUDIO_EN = 1;  // low = speaker amp on; keep off

LGFX tft;
Ui ui;
NowPlaying model;
BleService ble;
std::mutex modelLock;

#if DEMO
static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF);
}

// Feed the real model path with a fake source so the UI is exercised end to end.
static void loadDemo() {
  const uint16_t conn = 1;
  const uint32_t id = 0xD3E0;
  model.onConnect(conn);
  proto::Meta m;
  m.trackId = id;
  m.title = "\xC3\x93lafur Arnalds \xE2\x80\x94 Saman (Long Title For Marquee Test)";
  m.artist = "\xE6\x9D\xB1\xE4\xBA\xAC Test Artist";
  m.album = "Demo Album";
  model.onMeta(conn, m);
  std::vector<uint8_t> h{0x01};
  put32(h, id);
  put32(h, (uint32_t)kDemoArtLen);
  model.onArt(conn, h.data(), h.size());
  for (size_t off = 0; off < kDemoArtLen; off += 500) {
    size_t n = std::min<size_t>(500, kDemoArtLen - off);
    std::vector<uint8_t> d{0x02};
    put32(d, id);
    put32(d, (uint32_t)off);
    d.insert(d.end(), kDemoArt + off, kDemoArt + off + n);
    model.onArt(conn, d.data(), d.size());
  }
  proto::State s;
  s.trackId = id;
  s.playing = true;
  s.local = true;
  s.source = proto::Source::Mac;
  s.positionMs = 30000;
  s.durationMs = 225000;
  model.onState(conn, s, millis());
}
#endif

static layout::Action pollTap(uint32_t now) {
  static TapDetector tap;
  int32_t x, y;
  bool down = tft.getTouch(&x, &y);
  return tap.update(down, now) ? layout::hitTest(ui.view(), x, y) : layout::Action::None;
}

void setup() {
  pinMode(PIN_AUDIO_EN, OUTPUT);
  digitalWrite(PIN_AUDIO_EN, HIGH);
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.setBrightness(255);
  ui.begin(tft);
#if DEMO
  loadDemo();
#else
  ble.begin(model, modelLock);
#endif
}

static proto::Cmd toCmd(layout::Button b) {
  switch (b) {
    case layout::Button::Prev: return proto::Cmd::Prev;
    case layout::Button::Next: return proto::Cmd::Next;
    default: return proto::Cmd::PlayPause;
  }
}

void loop() {
  uint32_t now = millis();
  layout::Action a = pollTap(now);
  if (a == layout::Action::ToggleView) {
    bool active;
    {
      std::lock_guard<std::mutex> g(modelLock);
      active = model.snapshot(now).hasActive;
    }
    if (active) ui.toggleView();  // a view toggle never sends a command
  }
  layout::Button b = layout::toButton(a);
  if (b != layout::Button::None) {
    Serial.printf("tap %d\n", static_cast<int>(b));
    ui.flashButton(b);
#if DEMO
    if (b == layout::Button::PlayPause) {
      std::lock_guard<std::mutex> g(modelLock);
      Snapshot s = model.snapshot(now);
      proto::State st;
      st.trackId = s.track.id;
      st.playing = !s.playing;
      st.local = true;
      st.source = s.source;
      st.positionMs = s.positionMs;
      st.durationMs = s.track.durationMs;
      model.onState(1, st, now);
    }
#else
    int conn;
    {
      std::lock_guard<std::mutex> g(modelLock);
      conn = model.activeConn();
    }
    if (conn >= 0) ble.sendCommand(conn, toCmd(b));
#endif
  }
  Snapshot snap;
  {
    std::lock_guard<std::mutex> g(modelLock);
    snap = model.snapshot(now);
  }
  ui.render(snap, now);
  delay(2);
}
