#include <unity.h>

#include "Version.h"

void setUp() {}
void tearDown() {}

void test_version_is_not_empty() {
  TEST_ASSERT_TRUE(knobify::kVersion[0] != '\0');
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_version_is_not_empty);
  return UNITY_END();
}
