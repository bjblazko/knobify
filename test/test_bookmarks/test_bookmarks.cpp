#include <unity.h>

#include <string>

#include "Bookmarks.h"

using knobify::resume::Bookmark;
using knobify::resume::Bookmarks;

void setUp() {}
void tearDown() {}

namespace {

Bookmark mark(const std::string &title, const std::string &track,
              uint32_t pos = 1000, uint32_t elapsed = 60) {
  Bookmark b;
  b.titleKey = title;
  b.trackPath = track;
  b.filePosition = pos;
  b.elapsedSeconds = elapsed;
  return b;
}

Bookmark hitchhiker(uint32_t pos = 1000) {
  return mark("/Audiobooks/Adams/Hitchhiker",
              "/Audiobooks/Adams/Hitchhiker/03 Three.mp3", pos, 120);
}

}  // namespace

void test_nothing_is_remembered_to_begin_with() {
  Bookmarks marks;
  Bookmark found;
  TEST_ASSERT_EQUAL(0, marks.size());
  TEST_ASSERT_FALSE(marks.lookup("/Audiobooks/Adams/Hitchhiker", found));
  TEST_ASSERT_FALSE(marks.dirty());
}

void test_a_noted_title_can_be_looked_up() {
  Bookmarks marks;
  marks.note(hitchhiker());
  Bookmark found;
  TEST_ASSERT_TRUE(marks.lookup("/Audiobooks/Adams/Hitchhiker", found));
  TEST_ASSERT_EQUAL_STRING("/Audiobooks/Adams/Hitchhiker/03 Three.mp3",
                           found.trackPath.c_str());
  TEST_ASSERT_EQUAL_UINT32(1000, found.filePosition);
  TEST_ASSERT_TRUE(marks.dirty());
}

void test_noting_a_title_again_replaces_its_position() {
  Bookmarks marks;
  marks.note(hitchhiker(1000));
  marks.note(hitchhiker(5000));
  TEST_ASSERT_EQUAL(1, marks.size());
  Bookmark found;
  marks.lookup("/Audiobooks/Adams/Hitchhiker", found);
  TEST_ASSERT_EQUAL_UINT32(5000, found.filePosition);
}

// The position is written every few seconds while a title plays; only an
// actual change is worth an NVS write.
void test_noting_an_unchanged_position_does_not_dirty_the_set() {
  Bookmarks marks;
  marks.note(hitchhiker());
  marks.markSaved();
  marks.note(hitchhiker());
  TEST_ASSERT_FALSE(marks.dirty());
}

void test_several_titles_are_remembered_independently() {
  Bookmarks marks;
  marks.note(mark("/Audiobooks/A", "/Audiobooks/A/01.mp3", 11));
  marks.note(mark("/RadioPlays/B", "/RadioPlays/B/02.mp3", 22));
  Bookmark found;
  TEST_ASSERT_TRUE(marks.lookup("/Audiobooks/A", found));
  TEST_ASSERT_EQUAL_UINT32(11, found.filePosition);
  TEST_ASSERT_TRUE(marks.lookup("/RadioPlays/B", found));
  TEST_ASSERT_EQUAL_UINT32(22, found.filePosition);
}

void test_the_oldest_title_is_dropped_past_the_limit() {
  Bookmarks marks;
  for (std::size_t i = 0; i < Bookmarks::kMaxEntries + 3; ++i) {
    const std::string n = std::to_string(i);
    marks.note(mark("/Audiobooks/" + n, "/Audiobooks/" + n + "/01.mp3"));
  }
  TEST_ASSERT_EQUAL(Bookmarks::kMaxEntries, marks.size());
  Bookmark found;
  // The first three are gone, the most recent is kept.
  TEST_ASSERT_FALSE(marks.lookup("/Audiobooks/0", found));
  TEST_ASSERT_FALSE(marks.lookup("/Audiobooks/2", found));
  TEST_ASSERT_TRUE(marks.lookup("/Audiobooks/3", found));
  TEST_ASSERT_TRUE(
      marks.lookup("/Audiobooks/" + std::to_string(Bookmarks::kMaxEntries + 2),
                   found));
}

// Coming back to an older title must keep it, not let it age out behind
// titles you have not touched since.
void test_looking_at_an_old_title_again_makes_it_recent() {
  Bookmarks marks;
  marks.note(mark("/Audiobooks/keep", "/Audiobooks/keep/01.mp3", 1));
  for (std::size_t i = 0; i < Bookmarks::kMaxEntries - 1; ++i) {
    const std::string n = std::to_string(i);
    marks.note(mark("/Audiobooks/" + n, "/Audiobooks/" + n + "/01.mp3"));
  }
  // "keep" is now the oldest; playing it again moves it to the front.
  marks.note(mark("/Audiobooks/keep", "/Audiobooks/keep/01.mp3", 999));
  marks.note(mark("/Audiobooks/new", "/Audiobooks/new/01.mp3"));

  Bookmark found;
  TEST_ASSERT_TRUE(marks.lookup("/Audiobooks/keep", found));
  TEST_ASSERT_EQUAL_UINT32(999, found.filePosition);
  TEST_ASSERT_FALSE(marks.lookup("/Audiobooks/0", found));
}

void test_a_finished_title_can_be_forgotten() {
  Bookmarks marks;
  marks.note(hitchhiker());
  marks.forget("/Audiobooks/Adams/Hitchhiker");
  Bookmark found;
  TEST_ASSERT_FALSE(marks.lookup("/Audiobooks/Adams/Hitchhiker", found));
  TEST_ASSERT_EQUAL(0, marks.size());
  // Forgetting something unknown is harmless.
  marks.markSaved();
  marks.forget("/nothing/here");
  TEST_ASSERT_FALSE(marks.dirty());
}

void test_empty_keys_are_ignored() {
  Bookmarks marks;
  marks.note(mark("", "/Audiobooks/A/01.mp3"));
  marks.note(mark("/Audiobooks/A", ""));
  TEST_ASSERT_EQUAL(0, marks.size());
  TEST_ASSERT_FALSE(marks.dirty());
}

// --- Codec ---

void test_codec_round_trips() {
  Bookmarks marks;
  marks.note(mark("/Audiobooks/A", "/Audiobooks/A/01.mp3", 11, 22));
  marks.note(mark("/RadioPlays/B \xC3\x84", "/RadioPlays/B \xC3\x84/02.ogg", 33, 44));

  Bookmarks restored;
  TEST_ASSERT_TRUE(restored.decode(marks.encode()));
  TEST_ASSERT_EQUAL(2, restored.size());
  Bookmark found;
  TEST_ASSERT_TRUE(restored.lookup("/RadioPlays/B \xC3\x84", found));
  TEST_ASSERT_EQUAL_STRING("/RadioPlays/B \xC3\x84/02.ogg", found.trackPath.c_str());
  TEST_ASSERT_EQUAL_UINT32(33, found.filePosition);
  TEST_ASSERT_EQUAL_UINT32(44, found.elapsedSeconds);
  // A freshly decoded set has nothing to write.
  TEST_ASSERT_FALSE(restored.dirty());
}

void test_an_empty_set_round_trips() {
  Bookmarks marks;
  Bookmarks restored;
  restored.note(hitchhiker());
  TEST_ASSERT_TRUE(restored.decode(marks.encode()));
  TEST_ASSERT_EQUAL(0, restored.size());
}

void test_decode_rejects_a_truncated_blob() {
  Bookmarks marks;
  marks.note(hitchhiker());
  const std::vector<uint8_t> good = marks.encode();
  for (std::size_t cut = 0; cut < good.size(); ++cut) {
    std::vector<uint8_t> bytes(good.begin(), good.begin() + static_cast<long>(cut));
    Bookmarks restored;
    TEST_ASSERT_FALSE(restored.decode(bytes));
    TEST_ASSERT_EQUAL(0, restored.size());
  }
}

void test_decode_rejects_bad_magic_version_and_trailing_bytes() {
  Bookmarks marks;
  marks.note(hitchhiker());

  std::vector<uint8_t> bytes = marks.encode();
  bytes[0] = 'X';
  Bookmarks a;
  TEST_ASSERT_FALSE(a.decode(bytes));

  bytes = marks.encode();
  bytes[4] = Bookmarks::kVersion + 1;
  Bookmarks b;
  TEST_ASSERT_FALSE(b.decode(bytes));

  bytes = marks.encode();
  bytes.push_back(0);
  Bookmarks c;
  TEST_ASSERT_FALSE(c.decode(bytes));
}

void test_decode_rejects_an_implausible_elapsed_time() {
  Bookmarks marks;
  marks.note(mark("/Audiobooks/A", "/Audiobooks/A/01.mp3", 0,
                  Bookmarks::kMaxElapsedSeconds + 1));
  Bookmarks restored;
  TEST_ASSERT_FALSE(restored.decode(marks.encode()));
}

void test_a_decoded_set_replaces_nothing_when_it_fails() {
  Bookmarks marks;
  marks.note(hitchhiker());
  TEST_ASSERT_FALSE(marks.decode(std::vector<uint8_t>{'j', 'u', 'n', 'k'}));
  Bookmark found;
  TEST_ASSERT_TRUE(marks.lookup("/Audiobooks/Adams/Hitchhiker", found));
}

// The blob has to fit NVS: very long paths drop the oldest entries rather
// than making the whole set unsaveable.
void test_encoding_stays_within_the_size_budget() {
  Bookmarks marks;
  const std::string longPath(400, 'x');
  for (std::size_t i = 0; i < Bookmarks::kMaxEntries; ++i) {
    const std::string n = std::to_string(i);
    marks.note(mark("/Audiobooks/" + longPath + n,
                    "/Audiobooks/" + longPath + n + "/01.mp3"));
  }
  const std::vector<uint8_t> bytes = marks.encode();
  TEST_ASSERT_TRUE(bytes.size() <= Bookmarks::kMaxEncodedSize);

  Bookmarks restored;
  TEST_ASSERT_TRUE(restored.decode(bytes));
  TEST_ASSERT_TRUE(restored.size() > 0);
  TEST_ASSERT_TRUE(restored.size() < Bookmarks::kMaxEntries);
  // What survives is the most recent, which is what a user is part-way
  // through.
  Bookmark found;
  TEST_ASSERT_TRUE(restored.lookup(
      "/Audiobooks/" + longPath + std::to_string(Bookmarks::kMaxEntries - 1),
      found));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_remembered_to_begin_with);
  RUN_TEST(test_a_noted_title_can_be_looked_up);
  RUN_TEST(test_noting_a_title_again_replaces_its_position);
  RUN_TEST(test_noting_an_unchanged_position_does_not_dirty_the_set);
  RUN_TEST(test_several_titles_are_remembered_independently);
  RUN_TEST(test_the_oldest_title_is_dropped_past_the_limit);
  RUN_TEST(test_looking_at_an_old_title_again_makes_it_recent);
  RUN_TEST(test_a_finished_title_can_be_forgotten);
  RUN_TEST(test_empty_keys_are_ignored);
  RUN_TEST(test_codec_round_trips);
  RUN_TEST(test_an_empty_set_round_trips);
  RUN_TEST(test_decode_rejects_a_truncated_blob);
  RUN_TEST(test_decode_rejects_bad_magic_version_and_trailing_bytes);
  RUN_TEST(test_decode_rejects_an_implausible_elapsed_time);
  RUN_TEST(test_a_decoded_set_replaces_nothing_when_it_fails);
  RUN_TEST(test_encoding_stays_within_the_size_budget);
  return UNITY_END();
}
