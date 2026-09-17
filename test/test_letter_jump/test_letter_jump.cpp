#include <unity.h>

#include <string>
#include <vector>

#include "LetterJump.h"
#include "LibraryScanner.h"

using knobify::library::LibraryIndex;
using knobify::navigation::LetterBucket;
using knobify::navigation::LetterJump;

void setUp() {}
void tearDown() {}

namespace {

// The keys a browse list hands in: folded the way that list is sorted.
std::vector<std::string> tagKeys(const std::vector<std::string> &names) {
  std::vector<std::string> keys;
  for (const auto &name : names) keys.push_back(LibraryIndex::nameSortKey(name));
  return keys;
}

std::vector<std::string> nameKeys(const std::vector<std::string> &names) {
  std::vector<std::string> keys;
  for (const auto &name : names) keys.push_back(LibraryIndex::foldAccents(name));
  return keys;
}

}  // namespace

void test_buckets_start_at_each_letter_change() {
  auto buckets = LetterJump::buckets(
      nameKeys({"Abba", "Air", "Beck", "Blur", "Cure"}), 0);
  TEST_ASSERT_EQUAL(3, static_cast<int>(buckets.size()));
  TEST_ASSERT_EQUAL('A', buckets[0].letter);
  TEST_ASSERT_EQUAL(0, buckets[0].row);
  TEST_ASSERT_EQUAL('B', buckets[1].letter);
  TEST_ASSERT_EQUAL(2, buckets[1].row);
  TEST_ASSERT_EQUAL('C', buckets[2].letter);
  TEST_ASSERT_EQUAL(4, buckets[2].row);
}

void test_rows_are_offset_by_leading_action_rows() {
  // A Shuffle row sits at row 0, so the first artist is row 1.
  auto buckets = LetterJump::buckets(nameKeys({"Abba", "Beck"}), 1);
  TEST_ASSERT_EQUAL(1, buckets[0].row);
  TEST_ASSERT_EQUAL(2, buckets[1].row);
}

void test_letter_follows_the_sort_key_not_the_raw_name() {
  // Both sort under C and B respectively, so that is where they jump.
  auto buckets = LetterJump::buckets(tagKeys({"The Cure", "Die Arzte"}), 0);
  TEST_ASSERT_EQUAL('C', buckets[0].letter);
  TEST_ASSERT_EQUAL('D', buckets[1].letter);
}

void test_accented_names_fold_to_their_base_letter() {
  auto buckets = LetterJump::buckets(tagKeys({"Ärzte", "Björk"}), 0);
  TEST_ASSERT_EQUAL('A', buckets[0].letter);
  TEST_ASSERT_EQUAL('B', buckets[1].letter);
}

void test_digits_and_symbols_share_one_bucket() {
  auto buckets = LetterJump::buckets(
      nameKeys({"2Pac", "50 Cent", "!!!", "Abba"}), 0);
  TEST_ASSERT_EQUAL(2, static_cast<int>(buckets.size()));
  TEST_ASSERT_EQUAL('#', buckets[0].letter);
  TEST_ASSERT_EQUAL(0, buckets[0].row);
  TEST_ASSERT_EQUAL('A', buckets[1].letter);
  TEST_ASSERT_EQUAL(3, buckets[1].row);
}

void test_two_ascending_runs_keep_their_own_boundaries() {
  // Files: folders first, then files -- A..Z twice over.
  auto buckets = LetterJump::buckets(
      nameKeys({"Alben/", "Beste/", "abend.mp3", "brief.mp3"}), 0);
  TEST_ASSERT_EQUAL(4, static_cast<int>(buckets.size()));
  TEST_ASSERT_EQUAL('A', buckets[0].letter);
  TEST_ASSERT_EQUAL('B', buckets[1].letter);
  TEST_ASSERT_EQUAL('A', buckets[2].letter);
  TEST_ASSERT_EQUAL(2, buckets[2].row);
  TEST_ASSERT_EQUAL('B', buckets[3].letter);
}

void test_short_or_single_letter_lists_are_not_offered() {
  std::vector<std::string> many(LetterJump::kMinRowsToOffer, "Abba");
  // Long enough, but every row is one letter: nothing to jump between.
  TEST_ASSERT_FALSE(
      LetterJump::eligible(LetterJump::buckets(nameKeys(many), 0), many.size()));
  // Several letters, but short enough to just turn through.
  auto few = nameKeys({"Abba", "Beck", "Cure"});
  TEST_ASSERT_FALSE(
      LetterJump::eligible(LetterJump::buckets(few, 0), few.size()));
}

void test_long_multi_letter_lists_are_offered() {
  std::vector<std::string> names;
  for (size_t i = 0; i < LetterJump::kMinRowsToOffer; ++i) {
    names.push_back(std::string(1, static_cast<char>('a' + i)) + "band");
  }
  auto keys = nameKeys(names);
  TEST_ASSERT_TRUE(
      LetterJump::eligible(LetterJump::buckets(keys, 0), keys.size()));
}

void test_open_starts_at_the_bucket_holding_the_current_row() {
  LetterJump jump;
  jump.setBuckets(LetterJump::buckets(
      nameKeys({"Abba", "Air", "Beck", "Blur", "Cure"}), 0));
  jump.open(3, 1000);  // "Blur" -- inside the B bucket.
  TEST_ASSERT_TRUE(jump.active());
  TEST_ASSERT_EQUAL('B', jump.letter());
  TEST_ASSERT_EQUAL(4, jump.turn(1, 1000));
  TEST_ASSERT_EQUAL('C', jump.letter());
}

void test_turn_clamps_at_both_ends() {
  LetterJump jump;
  jump.setBuckets(LetterJump::buckets(nameKeys({"Abba", "Beck", "Cure"}), 0));
  jump.open(0, 0);
  TEST_ASSERT_EQUAL(0, jump.turn(-5, 0));
  TEST_ASSERT_EQUAL('A', jump.letter());
  TEST_ASSERT_EQUAL(2, jump.turn(9, 0));
  TEST_ASSERT_EQUAL('C', jump.letter());
}

void test_mode_waits_longer_for_the_very_first_turn() {
  // Opening it is a finger on the glass; using it is a hand on the knob.
  // The gap between the two is longer than the between-turns timeout.
  LetterJump jump;
  jump.setBuckets(LetterJump::buckets(nameKeys({"Abba", "Beck"}), 0));
  jump.open(0, 1000);
  jump.tick(1000 + LetterJump::kIdleCloseMs);
  TEST_ASSERT_TRUE(jump.active());
  jump.tick(1000 + LetterJump::kFirstTurnMs - 1);
  TEST_ASSERT_TRUE(jump.active());
  jump.tick(1000 + LetterJump::kFirstTurnMs);
  TEST_ASSERT_FALSE(jump.active());
}

void test_mode_closes_after_idle_timeout() {
  LetterJump jump;
  jump.setBuckets(LetterJump::buckets(nameKeys({"Abba", "Beck"}), 0));
  jump.open(0, 1000);
  // Once turning has started, the shorter between-turns timeout applies.
  jump.turn(1, 2000);
  jump.tick(2000 + LetterJump::kIdleCloseMs - 1);
  TEST_ASSERT_TRUE(jump.active());
  jump.turn(1, 3000);
  jump.tick(3000 + LetterJump::kIdleCloseMs - 1);
  TEST_ASSERT_TRUE(jump.active());
  jump.tick(3000 + LetterJump::kIdleCloseMs);
  TEST_ASSERT_FALSE(jump.active());
}

void test_rebuilding_the_list_closes_the_mode() {
  LetterJump jump;
  jump.setBuckets(LetterJump::buckets(nameKeys({"Abba", "Beck"}), 0));
  jump.open(0, 0);
  jump.setBuckets(LetterJump::buckets(nameKeys({"Cure", "Devo"}), 0));
  TEST_ASSERT_FALSE(jump.active());
}

void test_turning_while_closed_changes_nothing() {
  LetterJump jump;
  jump.setBuckets(LetterJump::buckets(nameKeys({"Abba", "Beck"}), 0));
  TEST_ASSERT_EQUAL(-1, jump.turn(1, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_buckets_start_at_each_letter_change);
  RUN_TEST(test_rows_are_offset_by_leading_action_rows);
  RUN_TEST(test_letter_follows_the_sort_key_not_the_raw_name);
  RUN_TEST(test_accented_names_fold_to_their_base_letter);
  RUN_TEST(test_digits_and_symbols_share_one_bucket);
  RUN_TEST(test_two_ascending_runs_keep_their_own_boundaries);
  RUN_TEST(test_short_or_single_letter_lists_are_not_offered);
  RUN_TEST(test_long_multi_letter_lists_are_offered);
  RUN_TEST(test_open_starts_at_the_bucket_holding_the_current_row);
  RUN_TEST(test_turn_clamps_at_both_ends);
  RUN_TEST(test_mode_waits_longer_for_the_very_first_turn);
  RUN_TEST(test_mode_closes_after_idle_timeout);
  RUN_TEST(test_rebuilding_the_list_closes_the_mode);
  RUN_TEST(test_turning_while_closed_changes_nothing);
  return UNITY_END();
}
