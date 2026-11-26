#include <unity.h>
#include "tap_detector.h"

void setUp() {}
void tearDown() {}

void test_single_press_fires_once() {
  TapDetector t;
  TEST_ASSERT_TRUE(t.update(true, 1000));
  for (uint32_t ms = 1010; ms < 1200; ms += 10) TEST_ASSERT_FALSE(t.update(true, ms));
  TEST_ASSERT_FALSE(t.update(false, 1200));
}

void test_release_glitch_does_not_refire() {
  TapDetector t;
  TEST_ASSERT_TRUE(t.update(true, 1000));
  TEST_ASSERT_FALSE(t.update(false, 1030));  // controller drops a frame mid-press
  TEST_ASSERT_FALSE(t.update(true, 1050));
  TEST_ASSERT_FALSE(t.update(false, 1300));
}

void test_second_real_tap_fires() {
  TapDetector t;
  TEST_ASSERT_TRUE(t.update(true, 1000));
  TEST_ASSERT_FALSE(t.update(false, 1100));
  TEST_ASSERT_FALSE(t.update(false, 1300));
  TEST_ASSERT_TRUE(t.update(true, 1400));
}

void test_quick_retap_within_lockout_ignored() {
  TapDetector t;
  TEST_ASSERT_TRUE(t.update(true, 1000));
  TEST_ASSERT_FALSE(t.update(false, 1050));
  TEST_ASSERT_FALSE(t.update(false, 1150));
  TEST_ASSERT_FALSE(t.update(true, 1200));  // within 250 ms of the last tap
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single_press_fires_once);
  RUN_TEST(test_release_glitch_does_not_refire);
  RUN_TEST(test_second_real_tap_fires);
  RUN_TEST(test_quick_retap_within_lockout_ignored);
  return UNITY_END();
}
