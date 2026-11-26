#include <unity.h>
#include <vector>
#include "now_playing.h"

using namespace proto;

void setUp() {}
void tearDown() {}

static const uint16_t MAC = 1, PHONE = 2;

static State st(uint32_t id, bool playing, bool local, Source src, uint32_t pos = 0, uint32_t dur = 200000) {
  State s;
  s.trackId = id;
  s.playing = playing;
  s.local = local;
  s.source = src;
  s.positionMs = pos;
  s.durationMs = dur;
  return s;
}

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF);
}

static void sendArt(NowPlaying& np, uint16_t conn, uint32_t id, size_t len, bool complete = true) {
  std::vector<uint8_t> h{0x01};
  put32(h, id);
  put32(h, (uint32_t)len);
  np.onArt(conn, h.data(), h.size());
  std::vector<uint8_t> d{0x02};
  put32(d, id);
  put32(d, 0);
  size_t n = complete ? len : len / 2;
  for (size_t i = 0; i < n; i++) d.push_back((uint8_t)i);
  np.onArt(conn, d.data(), d.size());
}

void test_first_playing_source_becomes_active() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onState(MAC, st(10, true, true, Source::Mac), 0);
  TEST_ASSERT_EQUAL(MAC, np.activeConn());
  Snapshot s = np.snapshot(0);
  TEST_ASSERT_TRUE(s.hasActive);
  TEST_ASSERT_EQUAL(static_cast<int>(Source::Mac), static_cast<int>(s.source));
  TEST_ASSERT_TRUE(s.playing);
  TEST_ASSERT_EQUAL(1, s.connected);
}

void test_newer_playing_source_takes_over() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onConnect(PHONE);
  np.onState(MAC, st(10, true, true, Source::Mac), 0);
  np.onState(PHONE, st(20, true, true, Source::Phone), 100);
  TEST_ASSERT_EQUAL(PHONE, np.activeConn());
  TEST_ASSERT_EQUAL_UINT32(20, np.snapshot(100).track.id);
}

void test_paused_keeps_last_active() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onConnect(PHONE);
  np.onState(MAC, st(10, true, true, Source::Mac), 0);
  np.onState(PHONE, st(20, false, true, Source::Phone), 50);
  np.onState(MAC, st(10, false, true, Source::Mac), 100);
  TEST_ASSERT_EQUAL(MAC, np.activeConn());
  TEST_ASSERT_FALSE(np.snapshot(100).playing);
}

void test_same_track_prefers_local_and_holds_3s() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onConnect(PHONE);
  np.onState(MAC, st(10, true, false, Source::Mac), 0);
  np.onState(PHONE, st(10, true, true, Source::Phone), 1000);
  TEST_ASSERT_EQUAL(MAC, np.activeConn());
  np.onState(PHONE, st(10, true, true, Source::Phone), 3500);
  TEST_ASSERT_EQUAL(PHONE, np.activeConn());
  np.onState(MAC, st(10, true, false, Source::Mac), 4000);
  TEST_ASSERT_EQUAL(PHONE, np.activeConn());
}

void test_same_track_paused_active_yields() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onConnect(PHONE);
  np.onState(PHONE, st(10, true, true, Source::Phone), 0);
  np.onState(PHONE, st(10, false, true, Source::Phone), 5000);
  np.onState(MAC, st(10, true, true, Source::Mac), 6000);
  TEST_ASSERT_EQUAL(MAC, np.activeConn());
}

void test_disconnect_active_falls_back() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onConnect(PHONE);
  np.onState(PHONE, st(20, false, true, Source::Phone), 0);
  sendArt(np, PHONE, 20, 100, false);  // partial
  np.onState(MAC, st(10, true, true, Source::Mac), 100);
  TEST_ASSERT_EQUAL(MAC, np.activeConn());
  np.onDisconnect(MAC);
  TEST_ASSERT_EQUAL(PHONE, np.activeConn());
  Snapshot s = np.snapshot(200);
  TEST_ASSERT_TRUE(s.hasActive);
  TEST_ASSERT_TRUE(s.art == nullptr);
  np.onDisconnect(PHONE);
  s = np.snapshot(300);
  TEST_ASSERT_FALSE(s.hasActive);
  TEST_ASSERT_EQUAL(0, s.connected);
  TEST_ASSERT_EQUAL(-1, np.activeConn());
}

void test_progress_clamps_and_freezes() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onState(MAC, st(10, true, true, Source::Mac, 59000, 60000), 1000);
  TEST_ASSERT_EQUAL_UINT32(60000, np.snapshot(6000).positionMs);
  np.onState(MAC, st(10, false, true, Source::Mac, 30000, 60000), 7000);
  TEST_ASSERT_EQUAL_UINT32(30000, np.snapshot(12000).positionMs);
}

void test_meta_and_art_only_shown_for_matching_track() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onState(MAC, st(8, true, true, Source::Mac), 0);
  Meta m;
  m.trackId = 7;
  m.title = "Seven";
  np.onMeta(MAC, m);
  sendArt(np, MAC, 7, 300);
  Snapshot a = np.snapshot(10);
  TEST_ASSERT_TRUE(a.art == nullptr);
  TEST_ASSERT_EQUAL_STRING("", a.track.title.c_str());
  np.onState(MAC, st(7, true, true, Source::Mac), 20);
  Snapshot b = np.snapshot(30);
  TEST_ASSERT_TRUE(b.art != nullptr);
  TEST_ASSERT_EQUAL(300, (int)b.art->size());
  TEST_ASSERT_EQUAL_STRING("Seven", b.track.title.c_str());
  TEST_ASSERT_TRUE(a.artVersion != b.artVersion);
}


void test_snapshot_art_outlives_disconnect() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onState(MAC, st(7, true, true, Source::Mac), 0);
  sendArt(np, MAC, 7, 300);
  Snapshot s = np.snapshot(10);
  TEST_ASSERT_TRUE(s.art != nullptr);
  np.onDisconnect(MAC);  // BLE task frees the slot while the UI still decodes s.art
  TEST_ASSERT_EQUAL(300, (int)s.art->size());
  TEST_ASSERT_EQUAL_UINT8(299 & 0xFF, (*s.art)[299]);
}

void test_remote_duplicate_never_displaces_paused_local() {
  NowPlaying np;
  np.onConnect(PHONE);
  np.onConnect(MAC);
  np.onState(PHONE, st(7, true, true, Source::Phone), 0);
  np.onState(PHONE, st(7, false, true, Source::Phone), 10000);
  np.onState(MAC, st(7, false, false, Source::Mac), 10010);
  np.onState(MAC, st(7, true, false, Source::Mac), 20000);  // Mac remote-controls, resumes first
  TEST_ASSERT_EQUAL(PHONE, np.activeConn());
  np.onState(PHONE, st(7, true, true, Source::Phone), 20080);
  TEST_ASSERT_EQUAL(PHONE, np.activeConn());
  np.onState(PHONE, st(7, true, true, Source::Phone), 25080);
  TEST_ASSERT_EQUAL(PHONE, np.activeConn());
}

void test_state_newer_than_snapshot_time_does_not_jump() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onState(MAC, st(7, true, true, Source::Mac, 30000, 200000), 5001);
  TEST_ASSERT_EQUAL_UINT32(30000, np.snapshot(5000).positionMs);
}

void test_old_track_stays_shown_until_new_state() {
  NowPlaying np;
  np.onConnect(MAC);
  np.onState(MAC, st(1, true, true, Source::Mac), 0);
  Meta a; a.trackId = 1; a.title = "Old";
  np.onMeta(MAC, a);
  sendArt(np, MAC, 1, 100);
  Meta b; b.trackId = 2; b.title = "New";
  np.onMeta(MAC, b);
  sendArt(np, MAC, 2, 200);
  Snapshot s = np.snapshot(50);
  TEST_ASSERT_EQUAL_STRING("Old", s.track.title.c_str());
  TEST_ASSERT_TRUE(s.art != nullptr);
  TEST_ASSERT_EQUAL(100, (int)s.art->size());
  np.onState(MAC, st(2, true, true, Source::Mac), 60);
  s = np.snapshot(70);
  TEST_ASSERT_EQUAL_STRING("New", s.track.title.c_str());
  TEST_ASSERT_TRUE(s.art != nullptr);
  TEST_ASSERT_EQUAL(200, (int)s.art->size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_playing_source_becomes_active);
  RUN_TEST(test_newer_playing_source_takes_over);
  RUN_TEST(test_paused_keeps_last_active);
  RUN_TEST(test_same_track_prefers_local_and_holds_3s);
  RUN_TEST(test_same_track_paused_active_yields);
  RUN_TEST(test_disconnect_active_falls_back);
  RUN_TEST(test_progress_clamps_and_freezes);
  RUN_TEST(test_meta_and_art_only_shown_for_matching_track);
  RUN_TEST(test_snapshot_art_outlives_disconnect);
  RUN_TEST(test_remote_duplicate_never_displaces_paused_local);
  RUN_TEST(test_state_newer_than_snapshot_time_does_not_jump);
  RUN_TEST(test_old_track_stays_shown_until_new_state);
  return UNITY_END();
}
