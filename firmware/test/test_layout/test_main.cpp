#include <unity.h>
#include "layout.h"

using namespace layout;

void setUp() {}
void tearDown() {}

static int cx(const Rect& r) { return r.x + r.w / 2; }
static int cy(const Rect& r) { return r.y + r.h / 2; }

#define HIT(v, x, y) static_cast<int>(hitTest(View::v, (x), (y)))
#define ACT(a) static_cast<int>(Action::a)

void test_hit_buttons() {
  TEST_ASSERT_EQUAL(ACT(Prev), HIT(Main, cx(kPrev), cy(kPrev)));
  TEST_ASSERT_EQUAL(ACT(PlayPause), HIT(Main, cx(kPlay), cy(kPlay)));
  TEST_ASSERT_EQUAL(ACT(Next), HIT(Main, cx(kNext), cy(kNext)));
  TEST_ASSERT_EQUAL(ACT(None), HIT(Main, 5, 5));
  TEST_ASSERT_EQUAL(ACT(Next), HIT(Main, kNext.x + kNext.w + 3, kNext.y + 10));
}

void test_mainArtTapToggles() { TEST_ASSERT_EQUAL(ACT(ToggleView), HIT(Main, 88, 104)); }
void test_lyricsAreaTapToggles() { TEST_ASSERT_EQUAL(ACT(ToggleView), HIT(Lyrics, 160, 90)); }
void test_lyricsButtonsHit() {
  TEST_ASSERT_EQUAL(ACT(Prev), HIT(Lyrics, cx(kLPrev), cy(kLPrev)));
  TEST_ASSERT_EQUAL(ACT(PlayPause), HIT(Lyrics, cx(kLPlay), cy(kLPlay)));
  TEST_ASSERT_EQUAL(ACT(Next), HIT(Lyrics, cx(kLNext), cy(kLNext)));
}
void test_lyricsButtonSlop() { TEST_ASSERT_EQUAL(ACT(Next), HIT(Lyrics, 272 + 40 + 3, 208)); }
void test_lyricsStatusRowNone() { TEST_ASSERT_EQUAL(ACT(None), HIT(Lyrics, 160, 10)); }
void test_toButtonMaps() {
  TEST_ASSERT_EQUAL(static_cast<int>(Button::Next), static_cast<int>(toButton(Action::Next)));
  TEST_ASSERT_EQUAL(static_cast<int>(Button::None), static_cast<int>(toButton(Action::ToggleView)));
}

void test_format_time() {
  TEST_ASSERT_EQUAL_STRING("0:00", formatTime(0).c_str());
  TEST_ASSERT_EQUAL_STRING("1:05", formatTime(65000).c_str());
  TEST_ASSERT_EQUAL_STRING("1:02:05", formatTime(3725000).c_str());
}

void test_progress_width() {
  TEST_ASSERT_EQUAL(67, progressWidth(30000, 60000, 134));
  TEST_ASSERT_EQUAL(0, progressWidth(0, 0, 134));
  TEST_ASSERT_EQUAL(134, progressWidth(70000, 60000, 134));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hit_buttons);
  RUN_TEST(test_mainArtTapToggles);
  RUN_TEST(test_lyricsAreaTapToggles);
  RUN_TEST(test_lyricsButtonsHit);
  RUN_TEST(test_lyricsButtonSlop);
  RUN_TEST(test_lyricsStatusRowNone);
  RUN_TEST(test_toButtonMaps);
  RUN_TEST(test_format_time);
  RUN_TEST(test_progress_width);
  return UNITY_END();
}
