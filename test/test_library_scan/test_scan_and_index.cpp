#include <unity.h>

#include "Fakes.h"
#include "IndexCache.h"

using knobify::library::Album;
using knobify::library::Artist;
using knobify::library::computeSignature;
using knobify::library::FileEntry;
using knobify::library::FolderBrowser;
using knobify::library::FolderEntry;
using knobify::library::IndexCache;
using knobify::library::LibraryIndex;
using knobify::library::LibraryScanner;
using knobify::library::LibrarySignature;
using knobify::library::Track;

void setUp() {}
void tearDown() {}

namespace {

void appendU32BE(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void appendSynchsafe(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 21) & 0x7F));
  out.push_back(static_cast<uint8_t>((v >> 14) & 0x7F));
  out.push_back(static_cast<uint8_t>((v >> 7) & 0x7F));
  out.push_back(static_cast<uint8_t>(v & 0x7F));
}

void appendFrame(std::vector<uint8_t> &frames, const char *id,
                  const std::string &text) {
  std::vector<uint8_t> data;
  data.push_back(0);
  data.insert(data.end(), text.begin(), text.end());
  frames.insert(frames.end(), id, id + 4);
  appendU32BE(frames, static_cast<uint32_t>(data.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), data.begin(), data.end());
}

std::vector<uint8_t> buildTaggedMp3(const std::string &artist,
                                     const std::string &album,
                                     const std::string &title) {
  std::vector<uint8_t> frames;
  appendFrame(frames, "TIT2", title);
  appendFrame(frames, "TPE1", artist);
  appendFrame(frames, "TALB", album);

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  return file;
}

std::vector<uint8_t> buildTaggedMp3WithTrackAndYear(
    const std::string &artist, const std::string &album,
    const std::string &title, const std::string &track,
    const std::string &year) {
  std::vector<uint8_t> frames;
  appendFrame(frames, "TIT2", title);
  appendFrame(frames, "TPE1", artist);
  appendFrame(frames, "TALB", album);
  appendFrame(frames, "TRCK", track);
  appendFrame(frames, "TYER", year);

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  return file;
}

}  // namespace

void test_scanner_groups_tagged_files_by_artist_and_album() {
  FakeFileLister lister({
      {"/a.mp3", 100, 1},
      {"/b.mp3", 200, 2},
      {"/c.mp3", 300, 3},
  });
  FakeFileOpener opener;
  opener.put("/a.mp3", buildTaggedMp3("Artist One", "Album One", "Track A"));
  opener.put("/b.mp3", buildTaggedMp3("Artist One", "Album One", "Track B"));
  opener.put("/c.mp3", buildTaggedMp3("Artist Two", "Album Two", "Track C"));

  LibraryIndex index = LibraryScanner::scan(lister, opener);

  TEST_ASSERT_EQUAL_UINT32(2, index.artists.size());
  TEST_ASSERT_EQUAL_UINT32(2, index.albums.size());
  TEST_ASSERT_EQUAL_UINT32(3, index.tracks.size());

  // Both tracks from Artist One / Album One share one album id.
  TEST_ASSERT_EQUAL_UINT32(index.tracks[0].albumId, index.tracks[1].albumId);
  TEST_ASSERT_NOT_EQUAL(index.tracks[0].albumId, index.tracks[2].albumId);
}

void test_scanner_falls_back_to_unknown_for_untagged_files() {
  FakeFileLister lister({{"/x.mp3", 10, 1}, {"/y.mp3", 20, 2}});
  FakeFileOpener opener;
  opener.put("/x.mp3", std::vector<uint8_t>(16, 0x00));
  opener.put("/y.mp3", std::vector<uint8_t>(16, 0x00));

  LibraryIndex index = LibraryScanner::scan(lister, opener);

  TEST_ASSERT_EQUAL_UINT32(1, index.artists.size());
  TEST_ASSERT_EQUAL_STRING("Unknown Artist", index.artists[0].name.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, index.albums.size());
  TEST_ASSERT_EQUAL_UINT32(2, index.tracks.size());
}

void test_scanner_skips_unopenable_files() {
  FakeFileLister lister({{"/missing.mp3", 10, 1}});
  FakeFileOpener opener;  // Nothing registered -> open() returns nullptr.

  LibraryIndex index = LibraryScanner::scan(lister, opener);

  TEST_ASSERT_EQUAL_UINT32(0, index.tracks.size());
}

void test_tracks_for_sorted_by_track_number() {
  FakeFileLister lister({
      {"/a.mp3", 100, 1},
      {"/b.mp3", 200, 2},
      {"/c.mp3", 300, 3},
  });
  FakeFileOpener opener;
  // Listed out of order (3, 1, 2) -- tracksFor() must still return them
  // sorted by tag-provided track number, not scan/listing order.
  opener.put("/a.mp3", buildTaggedMp3WithTrackAndYear(
                            "Artist", "Album", "Third", "3", "2000"));
  opener.put("/b.mp3", buildTaggedMp3WithTrackAndYear(
                            "Artist", "Album", "First", "1", "2000"));
  opener.put("/c.mp3", buildTaggedMp3WithTrackAndYear(
                            "Artist", "Album", "Second", "2", "2000"));

  LibraryIndex index = LibraryScanner::scan(lister, opener);
  auto trackIds = index.tracksFor(0);

  TEST_ASSERT_EQUAL_UINT32(3, trackIds.size());
  TEST_ASSERT_EQUAL_STRING("First", index.tracks[trackIds[0]].title.c_str());
  TEST_ASSERT_EQUAL_STRING("Second", index.tracks[trackIds[1]].title.c_str());
  TEST_ASSERT_EQUAL_STRING("Third", index.tracks[trackIds[2]].title.c_str());
}

void test_albums_for_sorted_by_year() {
  FakeFileLister lister({{"/a.mp3", 100, 1}, {"/b.mp3", 200, 2}});
  FakeFileOpener opener;
  // Listed with the later album first -- albumsFor() must still return
  // them chronologically.
  opener.put("/a.mp3", buildTaggedMp3WithTrackAndYear(
                            "Artist", "Newer Album", "Song", "1", "2010"));
  opener.put("/b.mp3", buildTaggedMp3WithTrackAndYear(
                            "Artist", "Older Album", "Song", "1", "1995"));

  LibraryIndex index = LibraryScanner::scan(lister, opener);
  auto albumIds = index.albumsFor(0);

  TEST_ASSERT_EQUAL_UINT32(2, albumIds.size());
  TEST_ASSERT_EQUAL_STRING("Older Album",
                            index.albums[albumIds[0]].title.c_str());
  TEST_ASSERT_EQUAL_STRING("Newer Album",
                            index.albums[albumIds[1]].title.c_str());
}

void test_tag_reader_falls_back_to_filename_track_and_folder_year() {
  FakeFileLister lister(
      {{"/Music/Artist/1998 The Album/07 A Song.mp3", 10, 1}});
  FakeFileOpener opener;
  // No ID3 tag at all -- track number and year must come from the
  // filename ("07 ...") and the containing folder name ("1998 ...").
  opener.put("/Music/Artist/1998 The Album/07 A Song.mp3",
             std::vector<uint8_t>(16, 0x00));

  LibraryIndex index = LibraryScanner::scan(lister, opener);

  TEST_ASSERT_EQUAL_UINT32(1, index.tracks.size());
  TEST_ASSERT_EQUAL_UINT16(7, index.tracks[0].trackNumber);
  TEST_ASSERT_EQUAL_UINT32(1, index.albums.size());
  TEST_ASSERT_EQUAL_UINT16(1998, index.albums[0].year);
}

void test_folder_browser_filters_and_sorts() {
  FakeDirectoryReader reader;
  reader.put("/Music",
             {{"zeta.mp3", false},
              {"Notes.txt", false},
              {"Beta", true},
              {"alpha.ogg", false},
              {"Alpha", true}});

  auto result = FolderBrowser::list(reader, "/Music");

  TEST_ASSERT_EQUAL_UINT32(4, result.size());
  // Folders first, alphabetically, then files alphabetically. Notes.txt
  // is filtered out entirely (not audio, not a folder).
  TEST_ASSERT_EQUAL_STRING("Alpha", result[0].name.c_str());
  TEST_ASSERT_TRUE(result[0].isDirectory);
  TEST_ASSERT_EQUAL_STRING("Beta", result[1].name.c_str());
  TEST_ASSERT_TRUE(result[1].isDirectory);
  TEST_ASSERT_EQUAL_STRING("alpha.ogg", result[2].name.c_str());
  TEST_ASSERT_FALSE(result[2].isDirectory);
  TEST_ASSERT_EQUAL_STRING("zeta.mp3", result[3].name.c_str());
}

void test_signature_matches_for_identical_listing() {
  FakeFileLister a({{"/a.mp3", 100, 1}, {"/b.mp3", 200, 2}});
  FakeFileLister b({{"/a.mp3", 100, 1}, {"/b.mp3", 200, 2}});

  TEST_ASSERT_TRUE(computeSignature(a) == computeSignature(b));
}

void test_signature_changes_when_a_file_changes() {
  FakeFileLister a({{"/a.mp3", 100, 1}, {"/b.mp3", 200, 2}});
  FakeFileLister b({{"/a.mp3", 100, 1}, {"/b.mp3", 999, 2}});

  TEST_ASSERT_TRUE(computeSignature(a) != computeSignature(b));
}

void test_index_cache_round_trips() {
  LibraryIndex index;
  index.artists.push_back(Artist{0, "Artist One"});
  index.albums.push_back(Album{0, 0, "Album One"});
  index.tracks.push_back(Track{0, 0, "Track A", 1, "/a.mp3"});
  index.tracks.push_back(Track{1, 0, "Track B", 2, "/b.mp3"});
  LibrarySignature sig{2, 12345};

  auto bytes = IndexCache::encode(index, sig);

  LibraryIndex decoded;
  LibrarySignature decodedSig;
  bool ok = IndexCache::decode(bytes, decoded, decodedSig);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(sig == decodedSig);
  TEST_ASSERT_EQUAL_UINT32(1, decoded.artists.size());
  TEST_ASSERT_EQUAL_STRING("Artist One", decoded.artists[0].name.c_str());
  TEST_ASSERT_EQUAL_UINT32(2, decoded.tracks.size());
  TEST_ASSERT_EQUAL_STRING("/b.mp3", decoded.tracks[1].filePath.c_str());
  TEST_ASSERT_EQUAL_UINT16(2, decoded.tracks[1].trackNumber);
}

void test_index_cache_rejects_corrupt_buffer() {
  std::vector<uint8_t> garbage(10, 0xFF);
  LibraryIndex decoded;
  LibrarySignature sig;

  bool ok = IndexCache::decode(garbage, decoded, sig);

  TEST_ASSERT_FALSE(ok);
}

void test_index_cache_rejects_truncated_buffer() {
  LibraryIndex index;
  index.artists.push_back(Artist{0, "Someone"});
  auto bytes = IndexCache::encode(index, LibrarySignature{1, 1});
  bytes.resize(bytes.size() - 5);  // Chop off the end.

  LibraryIndex decoded;
  LibrarySignature sig;
  bool ok = IndexCache::decode(bytes, decoded, sig);

  TEST_ASSERT_FALSE(ok);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_scanner_groups_tagged_files_by_artist_and_album);
  RUN_TEST(test_scanner_falls_back_to_unknown_for_untagged_files);
  RUN_TEST(test_scanner_skips_unopenable_files);
  RUN_TEST(test_tracks_for_sorted_by_track_number);
  RUN_TEST(test_albums_for_sorted_by_year);
  RUN_TEST(test_tag_reader_falls_back_to_filename_track_and_folder_year);
  RUN_TEST(test_folder_browser_filters_and_sorts);
  RUN_TEST(test_signature_matches_for_identical_listing);
  RUN_TEST(test_signature_changes_when_a_file_changes);
  RUN_TEST(test_index_cache_round_trips);
  RUN_TEST(test_index_cache_rejects_corrupt_buffer);
  RUN_TEST(test_index_cache_rejects_truncated_buffer);
  return UNITY_END();
}
