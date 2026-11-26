#include <unity.h>
#include "layout.h"

using namespace layout;

void setUp() {}
void tearDown() {}

static int cx(const Rect& r) { return r.x + r.w / 2; }
static int cy(const Rect& r) { return r.y + r.h / 2; }

void test_hit_buttons() {
  TEST_ASSERT_EQUAL(static_cast<int>(Button::Prev), static_cast<int>(hitTest(cx(kPrev), cy(kPrev))));
  TEST_ASSERT_EQUAL(static_cast<int>(Button::PlayPause), static_cast<int>(hitTest(cx(kPlay), cy(kPlay))));
  TEST_ASSERT_EQUAL(static_cast<int>(Button::Next), static_cast<int>(hitTest(cx(kNext), cy(kNext))));
  TEST_ASSERT_EQUAL(static_cast<int>(Button::None), static_cast<int>(hitTest(5, 5)));
  TEST_ASSERT_EQUAL(static_cast<int>(Button::Next), static_cast<int>(hitTest(kNext.x + kNext.w + 3, kNext.y + 10)));
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
  RUN_TEST(test_format_time);
  RUN_TEST(test_progress_width);
  return UNITY_END();
}
