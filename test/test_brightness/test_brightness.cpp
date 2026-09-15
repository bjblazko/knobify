#include <unity.h>

#include <map>
#include <string>

#include "BrightnessSetting.h"

using knobify::playback::KeyValueStore;
using knobify::power::BrightnessSetting;

void setUp() {}
void tearDown() {}

namespace {

class FakeStore : public KeyValueStore {
 public:
  bool getU8(const std::string &key, uint8_t &out) override {
    auto it = values.find(key);
    if (it == values.end()) return false;
    out = it->second;
    return true;
  }
  void setU8(const std::string &key, uint8_t value) override {
    values[key] = value;
    writes++;
  }
  std::map<std::string, uint8_t> values;
  int writes = 0;
};

}  // namespace

void test_defaults_to_full_brightness_when_nothing_stored() {
  FakeStore store;
  BrightnessSetting brightness(store);
  brightness.begin();
  TEST_ASSERT_EQUAL_UINT8(BrightnessSetting::kMaxLevel, brightness.level());
  TEST_ASSERT_EQUAL_UINT8(100, brightness.percent());
  TEST_ASSERT_EQUAL_UINT8(255, brightness.duty());
}

void test_loads_stored_level_and_clamps_garbage() {
  FakeStore store;
  store.values[BrightnessSetting::kKey] = 4;
  BrightnessSetting brightness(store);
  brightness.begin();
  TEST_ASSERT_EQUAL_UINT8(4, brightness.level());

  store.values[BrightnessSetting::kKey] = 0;
  brightness.begin();
  TEST_ASSERT_EQUAL_UINT8(BrightnessSetting::kMinLevel, brightness.level());

  store.values[BrightnessSetting::kKey] = 200;
  brightness.begin();
  TEST_ASSERT_EQUAL_UINT8(BrightnessSetting::kMaxLevel, brightness.level());
}

void test_adjust_clamps_at_both_ends() {
  FakeStore store;
  BrightnessSetting brightness(store);
  brightness.begin();
  brightness.adjust(5, 0);
  TEST_ASSERT_EQUAL_UINT8(BrightnessSetting::kMaxLevel, brightness.level());
  brightness.adjust(-50, 0);
  TEST_ASSERT_EQUAL_UINT8(BrightnessSetting::kMinLevel, brightness.level());
  TEST_ASSERT_EQUAL_UINT8(10, brightness.percent());
}

void test_duty_strictly_rises_and_never_reaches_zero() {
  FakeStore store;
  BrightnessSetting brightness(store);
  brightness.begin();
  brightness.adjust(-50, 0);
  uint8_t previous = 0;
  for (int level = BrightnessSetting::kMinLevel;
       level <= BrightnessSetting::kMaxLevel; ++level) {
    TEST_ASSERT_TRUE(brightness.duty() > previous);
    previous = brightness.duty();
    brightness.adjust(1, 0);
  }
}

void test_saves_only_after_changes_settle() {
  FakeStore store;
  BrightnessSetting brightness(store);
  brightness.begin();
  brightness.adjust(-1, 1000);
  brightness.tick(1500);
  brightness.adjust(-1, 1500);
  brightness.tick(2400);
  TEST_ASSERT_EQUAL_INT(0, store.writes);

  brightness.tick(2500);
  TEST_ASSERT_EQUAL_INT(1, store.writes);
  TEST_ASSERT_EQUAL_UINT8(8, store.values[BrightnessSetting::kKey]);

  brightness.tick(9000);
  TEST_ASSERT_EQUAL_INT(1, store.writes);
}

void test_adjust_without_change_does_not_save() {
  FakeStore store;
  BrightnessSetting brightness(store);
  brightness.begin();
  brightness.adjust(1, 0);  // Already at max.
  brightness.tick(5000);
  TEST_ASSERT_EQUAL_INT(0, store.writes);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_full_brightness_when_nothing_stored);
  RUN_TEST(test_loads_stored_level_and_clamps_garbage);
  RUN_TEST(test_adjust_clamps_at_both_ends);
  RUN_TEST(test_duty_strictly_rises_and_never_reaches_zero);
  RUN_TEST(test_saves_only_after_changes_settle);
  RUN_TEST(test_adjust_without_change_does_not_save);
  return UNITY_END();
}
