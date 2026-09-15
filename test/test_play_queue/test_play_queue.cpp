#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include "PlayQueue.h"

using knobify::playback::PlayQueue;
using knobify::playback::RepeatMode;

void setUp() {}
void tearDown() {}

namespace {

std::vector<std::string> tracks(int n) {
  std::vector<std::string> result;
  for (int i = 0; i < n; ++i) result.push_back("/" + std::to_string(i) + ".mp3");
  return result;
}

// Paths in play order from the current track to the end, stepping next()
// with repeat off.
std::vector<std::string> drain(PlayQueue &queue) {
  std::vector<std::string> order{queue.current()};
  while (queue.next(RepeatMode::Off)) order.push_back(queue.current());
  return order;
}

}  // namespace

void test_in_order_by_default_starting_at_start_index() {
  PlayQueue queue;
  queue.load(tracks(4), 1, false, 42);

  TEST_ASSERT_FALSE(queue.shuffled());
  auto order = drain(queue);
  TEST_ASSERT_EQUAL(3, order.size());
  TEST_ASSERT_EQUAL_STRING("/1.mp3", order[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/3.mp3", order[2].c_str());
}

void test_shuffled_load_is_a_permutation_of_everything() {
  PlayQueue queue;
  auto all = tracks(20);
  queue.load(all, 0, true, 42);

  TEST_ASSERT_TRUE(queue.shuffled());
  auto order = drain(queue);
  TEST_ASSERT_EQUAL(20, order.size());
  TEST_ASSERT_TRUE(order != all);  // Seed 42 doesn't yield the identity.
  std::sort(order.begin(), order.end());
  std::sort(all.begin(), all.end());
  TEST_ASSERT_TRUE(order == all);
}

void test_shuffle_on_keeps_current_track_and_shuffles_the_rest_after_it() {
  PlayQueue queue;
  auto all = tracks(20);
  queue.load(all, 5, false, 7);
  queue.next(RepeatMode::Off);  // Now at /6.

  queue.setShuffled(true);

  TEST_ASSERT_EQUAL_STRING("/6.mp3", queue.current().c_str());
  TEST_ASSERT_FALSE(queue.prev(RepeatMode::Off));  // Current is first now.
  auto order = drain(queue);
  TEST_ASSERT_EQUAL(20, order.size());
  std::sort(order.begin(), order.end());
  std::sort(all.begin(), all.end());
  TEST_ASSERT_TRUE(order == all);
}

void test_shuffle_off_resumes_original_order_after_current_track() {
  PlayQueue queue;
  queue.load(tracks(10), 0, true, 3);
  queue.next(RepeatMode::Off);
  queue.next(RepeatMode::Off);
  std::string current = queue.current();

  queue.setShuffled(false);

  TEST_ASSERT_EQUAL_STRING(current.c_str(), queue.current().c_str());
  int currentNumber = std::stoi(current.substr(1));
  if (currentNumber < 9) {
    TEST_ASSERT_TRUE(queue.next(RepeatMode::Off));
    TEST_ASSERT_EQUAL_STRING(
        ("/" + std::to_string(currentNumber + 1) + ".mp3").c_str(),
        queue.current().c_str());
  }
}

void test_next_and_prev_stop_at_the_ends_without_repeat() {
  PlayQueue queue;
  queue.load(tracks(2), 0, false, 1);

  TEST_ASSERT_FALSE(queue.prev(RepeatMode::Off));
  TEST_ASSERT_TRUE(queue.next(RepeatMode::Off));
  TEST_ASSERT_FALSE(queue.next(RepeatMode::Off));
  TEST_ASSERT_FALSE(queue.onFinished(RepeatMode::Off));
  TEST_ASSERT_EQUAL_STRING("/1.mp3", queue.current().c_str());
}

void test_repeat_all_wraps_both_ways_and_on_finish() {
  PlayQueue queue;
  queue.load(tracks(3), 2, false, 1);

  TEST_ASSERT_TRUE(queue.next(RepeatMode::All));
  TEST_ASSERT_EQUAL_STRING("/0.mp3", queue.current().c_str());
  TEST_ASSERT_TRUE(queue.prev(RepeatMode::All));
  TEST_ASSERT_EQUAL_STRING("/2.mp3", queue.current().c_str());
  TEST_ASSERT_TRUE(queue.onFinished(RepeatMode::All));
  TEST_ASSERT_EQUAL_STRING("/0.mp3", queue.current().c_str());
}

void test_repeat_one_replays_on_finish_but_next_still_skips() {
  PlayQueue queue;
  queue.load(tracks(3), 0, false, 1);

  TEST_ASSERT_TRUE(queue.onFinished(RepeatMode::One));
  TEST_ASSERT_EQUAL_STRING("/0.mp3", queue.current().c_str());
  TEST_ASSERT_TRUE(queue.next(RepeatMode::One));
  TEST_ASSERT_EQUAL_STRING("/1.mp3", queue.current().c_str());
}

void test_repeat_one_skips_like_repeat_all_at_the_end() {
  PlayQueue queue;
  queue.load(tracks(2), 1, false, 1);

  TEST_ASSERT_TRUE(queue.next(RepeatMode::One));
  TEST_ASSERT_EQUAL_STRING("/0.mp3", queue.current().c_str());
}

void test_empty_queue_is_inert() {
  PlayQueue queue;
  TEST_ASSERT_TRUE(queue.empty());
  TEST_ASSERT_FALSE(queue.next(RepeatMode::All));
  TEST_ASSERT_FALSE(queue.onFinished(RepeatMode::One));
  queue.setShuffled(true);
  TEST_ASSERT_TRUE(queue.empty());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_in_order_by_default_starting_at_start_index);
  RUN_TEST(test_shuffled_load_is_a_permutation_of_everything);
  RUN_TEST(test_shuffle_on_keeps_current_track_and_shuffles_the_rest_after_it);
  RUN_TEST(test_shuffle_off_resumes_original_order_after_current_track);
  RUN_TEST(test_next_and_prev_stop_at_the_ends_without_repeat);
  RUN_TEST(test_repeat_all_wraps_both_ways_and_on_finish);
  RUN_TEST(test_repeat_one_replays_on_finish_but_next_still_skips);
  RUN_TEST(test_repeat_one_skips_like_repeat_all_at_the_end);
  RUN_TEST(test_empty_queue_is_inert);
  return UNITY_END();
}
