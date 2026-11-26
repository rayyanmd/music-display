#include <unity.h>

void setUp() {}
void tearDown() {}

void test_truth() { TEST_ASSERT_TRUE(true); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_truth);
  return UNITY_END();
}
