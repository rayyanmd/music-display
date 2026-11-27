#include <unity.h>
#include <vector>
#include "lyrics.h"

using proto::LyricLine;

void setUp() {}
void tearDown() {}

static std::vector<LyricLine> three() { return {{1000, "a"}, {2000, "b"}, {3000, "c"}}; }

void test_currentLineBeforeFirstIsMinusOne() { TEST_ASSERT_EQUAL(-1, lyrics::currentLine(three(), 500)); }
void test_currentLineExactBoundary() { TEST_ASSERT_EQUAL(1, lyrics::currentLine(three(), 2000)); }
void test_currentLineAfterLast() { TEST_ASSERT_EQUAL(2, lyrics::currentLine(three(), 9000)); }
void test_currentLineFollowsBackwardSeek() {
  auto l = three();
  TEST_ASSERT_EQUAL(1, lyrics::currentLine(l, 2500));
  TEST_ASSERT_EQUAL(0, lyrics::currentLine(l, 1200));
}
void test_currentLineEmpty() { TEST_ASSERT_EQUAL(-1, lyrics::currentLine({}, 1000)); }
void test_currentLineDuplicateTimes() {
  std::vector<LyricLine> l{{1000, "a"}, {1000, "b"}, {2000, "c"}};
  TEST_ASSERT_EQUAL(1, lyrics::currentLine(l, 1500));
}
void test_plainTopLineProportional() {
  TEST_ASSERT_EQUAL(0, lyrics::plainTopLine(20, 0, 100000, 5));
  TEST_ASSERT_EQUAL(8, lyrics::plainTopLine(20, 50000, 100000, 5));
  TEST_ASSERT_EQUAL(15, lyrics::plainTopLine(20, 100000, 100000, 5));
  TEST_ASSERT_EQUAL(0, lyrics::plainTopLine(3, 50000, 100000, 5));
  TEST_ASSERT_EQUAL(0, lyrics::plainTopLine(20, 50000, 0, 5));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_currentLineBeforeFirstIsMinusOne);
  RUN_TEST(test_currentLineExactBoundary);
  RUN_TEST(test_currentLineAfterLast);
  RUN_TEST(test_currentLineFollowsBackwardSeek);
  RUN_TEST(test_currentLineEmpty);
  RUN_TEST(test_currentLineDuplicateTimes);
  RUN_TEST(test_plainTopLineProportional);
  return UNITY_END();
}
