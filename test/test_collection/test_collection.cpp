#include <unity.h>

#include <cstring>
#include <set>
#include <string>

#include "CollectionId.h"
#include "CollectionProfile.h"

using knobify::collection::CollectionId;
using knobify::collection::isValidCollection;
using knobify::collection::kCollectionCount;
using knobify::collection::kCollections;
using knobify::collection::profileOf;
using knobify::collection::SortOrder;

void setUp() {}
void tearDown() {}

void test_every_collection_has_its_own_root_and_cache() {
  std::set<std::string> roots;
  std::set<std::string> caches;
  for (const auto &profile : kCollections) {
    roots.insert(profile.rootPath);
    caches.insert(profile.cachePath);
  }
  TEST_ASSERT_EQUAL(kCollectionCount, roots.size());
  TEST_ASSERT_EQUAL(kCollectionCount, caches.size());
}

void test_no_root_is_a_prefix_of_another() {
  // "/Music" and "/MusicVideos" would make one collection's scan pick up
  // the other's files.
  for (const auto &a : kCollections) {
    for (const auto &b : kCollections) {
      if (a.id == b.id) continue;
      std::string outer = std::string(a.rootPath) + "/";
      TEST_ASSERT_NOT_EQUAL(0, strncmp(b.rootPath, outer.c_str(), outer.size()));
    }
  }
}

void test_roots_are_absolute_and_not_slash_terminated() {
  for (const auto &profile : kCollections) {
    TEST_ASSERT_EQUAL('/', profile.rootPath[0]);
    size_t length = strlen(profile.rootPath);
    TEST_ASSERT_TRUE(length > 1);
    TEST_ASSERT_NOT_EQUAL('/', profile.rootPath[length - 1]);
  }
}

void test_profile_lookup_matches_the_table_order() {
  // indexOf() indexes the table directly, so a row out of enum order
  // would hand out the wrong profile everywhere.
  for (const auto &profile : kCollections) {
    TEST_ASSERT_EQUAL(static_cast<int>(profile.id),
                      static_cast<int>(profileOf(profile.id).id));
  }
}

void test_music_keeps_the_historical_cache_path() {
  // Changing it would silently invalidate every existing card's index.
  const auto &music = profileOf(CollectionId::Music);
  TEST_ASSERT_EQUAL_STRING("/Music", music.rootPath);
  TEST_ASSERT_EQUAL_STRING("/knobify/library.idx", music.cachePath);
}

void test_spoken_word_collections_do_not_shuffle_and_resume_in_place() {
  for (CollectionId id : {CollectionId::Audiobooks, CollectionId::RadioPlays}) {
    const auto &profile = profileOf(id);
    TEST_ASSERT_FALSE(profile.hasShuffleRow);
    TEST_ASSERT_TRUE(profile.resumesWithinTitle);
    TEST_ASSERT_EQUAL(static_cast<int>(SortOrder::ByName),
                      static_cast<int>(profile.sort));
  }
}

void test_music_shuffles_and_sorts_by_tag() {
  const auto &music = profileOf(CollectionId::Music);
  TEST_ASSERT_TRUE(music.hasShuffleRow);
  TEST_ASSERT_FALSE(music.resumesWithinTitle);
  TEST_ASSERT_EQUAL(static_cast<int>(SortOrder::ByTag),
                    static_cast<int>(music.sort));
}

void test_only_known_bytes_name_a_collection() {
  TEST_ASSERT_TRUE(isValidCollection(0));
  TEST_ASSERT_TRUE(isValidCollection(kCollectionCount - 1));
  TEST_ASSERT_FALSE(isValidCollection(kCollectionCount));
  TEST_ASSERT_FALSE(isValidCollection(255));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_every_collection_has_its_own_root_and_cache);
  RUN_TEST(test_no_root_is_a_prefix_of_another);
  RUN_TEST(test_roots_are_absolute_and_not_slash_terminated);
  RUN_TEST(test_profile_lookup_matches_the_table_order);
  RUN_TEST(test_music_keeps_the_historical_cache_path);
  RUN_TEST(test_spoken_word_collections_do_not_shuffle_and_resume_in_place);
  RUN_TEST(test_music_shuffles_and_sorts_by_tag);
  RUN_TEST(test_only_known_bytes_name_a_collection);
  return UNITY_END();
}
