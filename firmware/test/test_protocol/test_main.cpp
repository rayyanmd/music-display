#include <unity.h>
#include <string>
#include <vector>
#include "protocol.h"

using namespace proto;

void setUp() {}
void tearDown() {}

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF);
}

static std::vector<uint8_t> metaBytes(uint32_t id, const std::string& s) {
  std::vector<uint8_t> v;
  put32(v, id);
  v.insert(v.end(), s.begin(), s.end());
  return v;
}

static std::vector<uint8_t> artHeader(uint32_t id, uint32_t len) {
  std::vector<uint8_t> v{0x01};
  put32(v, id);
  put32(v, len);
  return v;
}

static std::vector<uint8_t> artData(uint32_t id, uint32_t off, const uint8_t* p, size_t n) {
  std::vector<uint8_t> v{0x02};
  put32(v, id);
  put32(v, off);
  v.insert(v.end(), p, p + n);
  return v;
}

void test_state_roundtrip() {
  const uint8_t b[16] = {1, 0b11, 1, 0, 0x78, 0x56, 0x34, 0x12, 0xE8, 0x03, 0, 0, 0x60, 0xEA, 0, 0};
  State s{};
  TEST_ASSERT_TRUE(decodeState(b, sizeof b, s));
  TEST_ASSERT_EQUAL_HEX32(0x12345678, s.trackId);
  TEST_ASSERT_EQUAL_UINT32(1000, s.positionMs);
  TEST_ASSERT_EQUAL_UINT32(60000, s.durationMs);
  TEST_ASSERT_TRUE(s.playing);
  TEST_ASSERT_TRUE(s.local);
  TEST_ASSERT_EQUAL(static_cast<int>(Source::Mac), static_cast<int>(s.source));
}

void test_state_too_short() {
  const uint8_t b[16] = {1, 0b11, 1, 0, 1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0};
  State s{};
  TEST_ASSERT_FALSE(decodeState(b, 15, s));
  uint8_t v2[16];
  for (int i = 0; i < 16; i++) v2[i] = b[i];
  v2[0] = 2;
  TEST_ASSERT_FALSE(decodeState(v2, 16, s));
}

void test_meta_three_fields() {
  auto v = metaBytes(42, "Song\x1F" "Artist\x1F" "Album");
  Meta m;
  TEST_ASSERT_TRUE(decodeMeta(v.data(), v.size(), m));
  TEST_ASSERT_EQUAL_UINT32(42, m.trackId);
  TEST_ASSERT_EQUAL_STRING("Song", m.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Artist", m.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Album", m.album.c_str());
}

void test_meta_missing_fields() {
  auto v = metaBytes(1, "Song");
  Meta m;
  TEST_ASSERT_TRUE(decodeMeta(v.data(), v.size(), m));
  TEST_ASSERT_EQUAL_STRING("Song", m.title.c_str());
  TEST_ASSERT_EQUAL_STRING("", m.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("", m.album.c_str());
  uint8_t tiny[3] = {1, 2, 3};
  TEST_ASSERT_FALSE(decodeMeta(tiny, 3, m));
}

void test_meta_utf8_and_truncation() {
  std::string longTitle;
  for (int i = 0; i < 200; i++) longTitle += "\xC3\xA9";  // é
  auto v = metaBytes(1, longTitle + "\x1F" "\xE6\x9D\xB1\xE4\xBA\xAC" "\x1F" "x");  // 東京
  Meta m;
  TEST_ASSERT_TRUE(decodeMeta(v.data(), v.size(), m));
  TEST_ASSERT_TRUE(m.title.size() <= kMaxField);
  TEST_ASSERT_TRUE(m.title.size() > 0);
  TEST_ASSERT_EQUAL(0, (int)(m.title.size() % 2));
  TEST_ASSERT_EQUAL_HEX8(0xA9, (uint8_t)m.title.back());
  TEST_ASSERT_EQUAL_STRING("\xE6\x9D\xB1\xE4\xBA\xAC", m.artist.c_str());
}

void test_art_complete() {
  std::vector<uint8_t> img(1200);
  for (size_t i = 0; i < img.size(); i++) img[i] = (uint8_t)(i * 7);
  ArtAssembler a;
  auto h = artHeader(7, 1200);
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::InProgress), static_cast<int>(a.feed(h.data(), h.size())));
  auto d0 = artData(7, 0, img.data(), 500);
  auto d1 = artData(7, 500, img.data() + 500, 500);
  auto d2 = artData(7, 1000, img.data() + 1000, 200);
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::InProgress), static_cast<int>(a.feed(d0.data(), d0.size())));
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::InProgress), static_cast<int>(a.feed(d1.data(), d1.size())));
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::Complete), static_cast<int>(a.feed(d2.data(), d2.size())));
  TEST_ASSERT_EQUAL_UINT32(7, a.trackId());
  TEST_ASSERT_EQUAL(1200, (int)a.data().size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(img.data(), a.data().data(), 1200);
}

void test_art_data_before_header_ignored() {
  uint8_t x[10] = {0};
  ArtAssembler a;
  auto d = artData(7, 0, x, 10);
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::Ignored), static_cast<int>(a.feed(d.data(), d.size())));
}

void test_art_offset_gap_errors() {
  uint8_t x[10] = {0};
  ArtAssembler a;
  auto h = artHeader(7, 1000);
  a.feed(h.data(), h.size());
  auto d = artData(7, 500, x, 10);
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::Error), static_cast<int>(a.feed(d.data(), d.size())));
  auto d0 = artData(7, 0, x, 10);
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::Ignored), static_cast<int>(a.feed(d0.data(), d0.size())));
}

void test_art_too_large_errors() {
  ArtAssembler a;
  auto h = artHeader(7, 40000);
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::Error), static_cast<int>(a.feed(h.data(), h.size())));
}

void test_art_new_header_restarts() {
  uint8_t x[100] = {0};
  ArtAssembler a;
  auto h7 = artHeader(7, 1000);
  a.feed(h7.data(), h7.size());
  auto d = artData(7, 0, x, 100);
  a.feed(d.data(), d.size());
  auto h8 = artHeader(8, 100);
  a.feed(h8.data(), h8.size());
  TEST_ASSERT_EQUAL_UINT32(8, a.trackId());
  TEST_ASSERT_EQUAL(0, (int)a.data().size());
  auto d8 = artData(8, 0, x, 100);
  TEST_ASSERT_EQUAL(static_cast<int>(ArtAssembler::Result::Complete), static_cast<int>(a.feed(d8.data(), d8.size())));
}

// ---- lyrics (v1.1) ----
static void putLine(std::vector<uint8_t>& v, uint32_t t, const std::string& s) {
  put32(v, t);
  v.push_back((uint8_t)s.size());
  v.insert(v.end(), s.begin(), s.end());
}

static std::vector<uint8_t> lyrHeader(uint32_t id, uint32_t total, uint8_t kind) {
  std::vector<uint8_t> v{0x01};
  put32(v, id);
  put32(v, total);
  v.push_back(kind);
  return v;
}

void test_lyricsBodyDecodesLines() {
  std::vector<uint8_t> b;
  putLine(b, 1000, "Hello");
  putLine(b, 2500, "W\xC3\xB6rld");
  std::vector<LyricLine> out;
  TEST_ASSERT_TRUE(decodeLyricsBody(b.data(), b.size(), out));
  TEST_ASSERT_EQUAL(2, out.size());
  TEST_ASSERT_EQUAL_UINT32(1000, out[0].timeMs);
  TEST_ASSERT_EQUAL_UINT32(2500, out[1].timeMs);
  TEST_ASSERT_EQUAL_STRING("W\xC3\xB6rld", out[1].text.c_str());
}

void test_lyricsBodyEmptyLineAllowed() {
  std::vector<uint8_t> b;
  putLine(b, 0, "");
  std::vector<LyricLine> out;
  TEST_ASSERT_TRUE(decodeLyricsBody(b.data(), b.size(), out));
  TEST_ASSERT_EQUAL(1, out.size());
  TEST_ASSERT_EQUAL_STRING("", out[0].text.c_str());
}

void test_lyricsBodyRejectsOverrun() {
  std::vector<uint8_t> b;
  put32(b, 0);
  b.push_back(10);
  b.push_back('a'); b.push_back('b'); b.push_back('c');
  std::vector<LyricLine> out;
  TEST_ASSERT_FALSE(decodeLyricsBody(b.data(), b.size(), out));
  std::vector<uint8_t> partial{1, 2};  // truncated time field
  TEST_ASSERT_FALSE(decodeLyricsBody(partial.data(), partial.size(), out));
}

void test_lyricsBodyRejectsLongLine() {
  std::vector<uint8_t> b;
  put32(b, 0);
  b.push_back(201);
  b.insert(b.end(), 201, 'x');
  std::vector<LyricLine> out;
  TEST_ASSERT_FALSE(decodeLyricsBody(b.data(), b.size(), out));
}

void test_lyricsBodyRejectsDecreasingTimes() {
  std::vector<uint8_t> b;
  putLine(b, 2000, "a");
  putLine(b, 1000, "b");
  std::vector<LyricLine> out;
  TEST_ASSERT_FALSE(decodeLyricsBody(b.data(), b.size(), out));
}

void test_lyricsAssemblerSyncedComplete() {
  std::vector<uint8_t> body;
  putLine(body, 1000, "one");
  putLine(body, 2000, "two");
  LyricsAssembler a;
  auto h = lyrHeader(9, body.size(), 1);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::InProgress, a.feed(h.data(), h.size()));
  size_t half = body.size() / 2;
  auto d1 = artData(9, 0, body.data(), half);
  auto d2 = artData(9, half, body.data() + half, body.size() - half);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::InProgress, a.feed(d1.data(), d1.size()));
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Complete, a.feed(d2.data(), d2.size()));
  TEST_ASSERT_EQUAL(LyricsKind::Synced, a.kind());
  TEST_ASSERT_EQUAL_UINT32(9, a.trackId());
  TEST_ASSERT_EQUAL(body.size(), a.data().size());
}

void test_lyricsAssemblerNoneCompletesOnHeader() {
  LyricsAssembler a;
  auto h = lyrHeader(5, 0, 0);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Complete, a.feed(h.data(), h.size()));
  TEST_ASSERT_EQUAL(LyricsKind::None, a.kind());
  auto i = lyrHeader(5, 0, 3);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Complete, a.feed(i.data(), i.size()));
  TEST_ASSERT_EQUAL(LyricsKind::Instrumental, a.kind());
  auto bad = lyrHeader(5, 4, 0);  // kind none must have no body
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Error, a.feed(bad.data(), bad.size()));
  auto empty = lyrHeader(5, 0, 1);  // synced must have a body
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Error, a.feed(empty.data(), empty.size()));
}

void test_lyricsHeaderRejectsOversize() {
  LyricsAssembler a;
  auto h = lyrHeader(1, 16385, 1);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Error, a.feed(h.data(), h.size()));
  auto ok = lyrHeader(1, 16384, 1);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::InProgress, a.feed(ok.data(), ok.size()));
}

void test_lyricsHeaderRejectsBadKind() {
  LyricsAssembler a;
  auto h = lyrHeader(1, 10, 4);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Error, a.feed(h.data(), h.size()));
}

void test_lyricsAssemblerGapAborts() {
  LyricsAssembler a;
  uint8_t p[8] = {0};
  auto h = lyrHeader(3, 16, 1);
  a.feed(h.data(), h.size());
  auto d = artData(3, 4, p, 8);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Error, a.feed(d.data(), d.size()));
  auto d0 = artData(3, 0, p, 8);
  TEST_ASSERT_EQUAL(LyricsAssembler::Result::Ignored, a.feed(d0.data(), d0.size()));
}

void test_lyricsBodyRejectsTooManyLines() {
  std::vector<uint8_t> b;
  for (int i = 0; i < 1000; i++) putLine(b, 0, "");
  std::vector<LyricLine> out;
  TEST_ASSERT_TRUE(decodeLyricsBody(b.data(), b.size(), out));
  TEST_ASSERT_EQUAL(1000, out.size());
  putLine(b, 0, "");
  TEST_ASSERT_FALSE(decodeLyricsBody(b.data(), b.size(), out));
  TEST_ASSERT_EQUAL(0, out.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_state_roundtrip);
  RUN_TEST(test_state_too_short);
  RUN_TEST(test_meta_three_fields);
  RUN_TEST(test_meta_missing_fields);
  RUN_TEST(test_meta_utf8_and_truncation);
  RUN_TEST(test_art_complete);
  RUN_TEST(test_art_data_before_header_ignored);
  RUN_TEST(test_art_offset_gap_errors);
  RUN_TEST(test_art_too_large_errors);
  RUN_TEST(test_art_new_header_restarts);
  RUN_TEST(test_lyricsBodyDecodesLines);
  RUN_TEST(test_lyricsBodyEmptyLineAllowed);
  RUN_TEST(test_lyricsBodyRejectsOverrun);
  RUN_TEST(test_lyricsBodyRejectsLongLine);
  RUN_TEST(test_lyricsBodyRejectsDecreasingTimes);
  RUN_TEST(test_lyricsAssemblerSyncedComplete);
  RUN_TEST(test_lyricsAssemblerNoneCompletesOnHeader);
  RUN_TEST(test_lyricsHeaderRejectsOversize);
  RUN_TEST(test_lyricsHeaderRejectsBadKind);
  RUN_TEST(test_lyricsAssemblerGapAborts);
  RUN_TEST(test_lyricsBodyRejectsTooManyLines);
  return UNITY_END();
}
