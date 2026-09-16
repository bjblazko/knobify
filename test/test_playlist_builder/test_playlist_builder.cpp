#include <unity.h>

#include "LibraryScanner.h"
#include "PlaylistBuilder.h"

using knobify::library::LibraryIndex;
using knobify::library::PlaylistBuilder;
using knobify::library::SortOrder;

void setUp() {}
void tearDown() {}

namespace {

// Ids are vector indices, like LibraryScanner builds them. Deliberately
// stored out of order so the builder has to sort like the list screens.
LibraryIndex makeIndex() {
  LibraryIndex index;
  index.artists = {{0, "Tool"}, {1, "Bjork"}};
  index.albums = {
      {0, 0, "Lateralus", 2001},
      {1, 0, "Undertow", 1993},
      {2, 1, "Debut", 1993},
  };
  index.tracks = {
      {0, 0, "Schism", 6, "/tool/lateralus/06.mp3"},
      {1, 0, "The Grudge", 1, "/tool/lateralus/01.mp3"},
      {2, 1, "Sober", 5, "/tool/undertow/05.mp3"},
      {3, 2, "Human Behaviour", 1, "/bjork/debut/01.mp3"},
  };
  return index;
}

}  // namespace

void test_album_is_in_track_number_order() {
  auto index = makeIndex();
  auto paths = PlaylistBuilder::forAlbum(index, 0);

  TEST_ASSERT_EQUAL(2, paths.size());
  TEST_ASSERT_EQUAL_STRING("/tool/lateralus/01.mp3", paths[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/tool/lateralus/06.mp3", paths[1].c_str());
}

void test_artist_is_albums_chronologically_then_tracks() {
  auto index = makeIndex();
  auto paths = PlaylistBuilder::forArtist(index, SortOrder::ByTag, 0);

  TEST_ASSERT_EQUAL(3, paths.size());
  TEST_ASSERT_EQUAL_STRING("/tool/undertow/05.mp3", paths[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/tool/lateralus/01.mp3", paths[1].c_str());
  TEST_ASSERT_EQUAL_STRING("/tool/lateralus/06.mp3", paths[2].c_str());
}

// The list screen shows artistsSorted(), so the playlist must follow the
// same order -- not the index's insertion order, which is SD scan order and
// means nothing to a listener.
void test_library_is_every_artist_in_list_order() {
  auto index = makeIndex();
  auto paths = PlaylistBuilder::forLibrary(index, SortOrder::ByTag);

  TEST_ASSERT_EQUAL(4, paths.size());
  TEST_ASSERT_EQUAL_STRING("/bjork/debut/01.mp3", paths[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/tool/undertow/05.mp3", paths[1].c_str());
  TEST_ASSERT_EQUAL_STRING("/tool/lateralus/01.mp3", paths[2].c_str());
  TEST_ASSERT_EQUAL_STRING("/tool/lateralus/06.mp3", paths[3].c_str());
}

// A spoken-word collection ignores the year: its episodes are followed by
// name, and their years are usually missing anyway (ADR 0018).
void test_by_name_orders_an_artists_albums_by_title_not_year() {
  auto index = makeIndex();
  auto paths = PlaylistBuilder::forArtist(index, SortOrder::ByName, 0);

  TEST_ASSERT_EQUAL(3, paths.size());
  TEST_ASSERT_EQUAL_STRING("/tool/lateralus/01.mp3", paths[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/tool/lateralus/06.mp3", paths[1].c_str());
  TEST_ASSERT_EQUAL_STRING("/tool/undertow/05.mp3", paths[2].c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_album_is_in_track_number_order);
  RUN_TEST(test_artist_is_albums_chronologically_then_tracks);
  RUN_TEST(test_library_is_every_artist_in_list_order);
  RUN_TEST(test_by_name_orders_an_artists_albums_by_title_not_year);
  return UNITY_END();
}
