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
using knobify::library::TagResult;
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

std::vector<uint8_t> buildTaggedMp3WithDisc(
    const std::string &artist, const std::string &album,
    const std::string &title, const std::string &track,
    const std::string &disc) {
  std::vector<uint8_t> frames;
  appendFrame(frames, "TIT2", title);
  appendFrame(frames, "TPE1", artist);
  appendFrame(frames, "TALB", album);
  appendFrame(frames, "TRCK", track);
  appendFrame(frames, "TPOS", disc);

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

void test_tracks_for_sorted_by_disc_then_track() {
  FakeFileLister lister({
      {"/a.mp3", 100, 1},
      {"/b.mp3", 200, 2},
      {"/c.mp3", 300, 3},
      {"/d.mp3", 400, 4},
  });
  FakeFileOpener opener;
  // A two-disc album listed interleaved -- track numbers restart per disc,
  // so sorting by track number alone would mix the discs.
  opener.put("/a.mp3", buildTaggedMp3WithDisc("Artist", "Box", "D2T1", "1/2", "2/2"));
  opener.put("/b.mp3", buildTaggedMp3WithDisc("Artist", "Box", "D1T2", "2/2", "1/2"));
  opener.put("/c.mp3", buildTaggedMp3WithDisc("Artist", "Box", "D2T2", "2/2", "2/2"));
  opener.put("/d.mp3", buildTaggedMp3WithDisc("Artist", "Box", "D1T1", "1/2", "1/2"));

  LibraryIndex index = LibraryScanner::scan(lister, opener);
  auto trackIds = index.tracksFor(0);

  TEST_ASSERT_EQUAL_UINT32(4, trackIds.size());
  TEST_ASSERT_EQUAL_STRING("D1T1", index.tracks[trackIds[0]].title.c_str());
  TEST_ASSERT_EQUAL_STRING("D1T2", index.tracks[trackIds[1]].title.c_str());
  TEST_ASSERT_EQUAL_STRING("D2T1", index.tracks[trackIds[2]].title.c_str());
  TEST_ASSERT_EQUAL_STRING("D2T2", index.tracks[trackIds[3]].title.c_str());
  TEST_ASSERT_EQUAL_UINT16(2, index.tracks[trackIds[2]].discNumber);
}

void test_tag_reader_falls_back_to_filename_disc_and_track() {
  FakeFileLister lister({{"/Music/Box/3-04 A Song.mp3", 10, 1}});
  FakeFileOpener opener;
  opener.put("/Music/Box/3-04 A Song.mp3", std::vector<uint8_t>(16, 0x00));

  LibraryIndex index = LibraryScanner::scan(lister, opener);

  TEST_ASSERT_EQUAL_UINT32(1, index.tracks.size());
  TEST_ASSERT_EQUAL_UINT16(3, index.tracks[0].discNumber);
  TEST_ASSERT_EQUAL_UINT16(4, index.tracks[0].trackNumber);
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

void test_untagged_files_fall_back_to_folder_names() {
  // Laid out the way the player expects: <root>/<Artist>/<Album>/track. A
  // radio play lives in its own collection now (ADR 0018), so its artist
  // level is the series, not a "Hörspiele" folder inside Music.
  FakeFileLister lister(
      {{"/RadioPlays/Der Herr der Ringe (WDR)/Teil 1/01 Kurztest.ogg", 10, 1},
       {"/Music/Air/1998 Moon Safari/02 Sexy Boy.mp3", 10, 2}});
  FakeFileOpener opener;
  opener.put("/RadioPlays/Der Herr der Ringe (WDR)/Teil 1/01 Kurztest.ogg",
             std::vector<uint8_t>{'n', 'o', 't', 'a', 'g', 's'});
  opener.put("/Music/Air/1998 Moon Safari/02 Sexy Boy.mp3",
             std::vector<uint8_t>{'n', 'o', 't', 'a', 'g', 's'});

  LibraryIndex index = LibraryScanner::scan(lister, opener);

  // The folders say what the missing tags don't: the album folder is the
  // album (without its year prefix), the one above it is the artist.
  bool foundHoerspiel = false;
  bool foundAir = false;
  for (const auto &album : index.albums) {
    const std::string artist = index.artists[album.artistId].name;
    if (album.title == "Teil 1") {
      foundHoerspiel = true;
      TEST_ASSERT_EQUAL_STRING("Der Herr der Ringe (WDR)", artist.c_str());
    }
    if (album.title == "Moon Safari") {
      foundAir = true;
      TEST_ASSERT_EQUAL_STRING("Air", artist.c_str());
      TEST_ASSERT_EQUAL_UINT16(1998, album.year);
    }
  }
  TEST_ASSERT_TRUE(foundHoerspiel);
  TEST_ASSERT_TRUE(foundAir);
}

void test_a_single_untagged_folder_names_both_artist_and_album() {
  // One folder under the library root, no tags -- e.g. an audio drama
  // dropped in as Music/dhdr/*.ogg. It should be findable under that
  // name, not buried in Unknown Artist.
  FakeFileLister lister({{"/Music/dhdr/01 Teil.ogg", 10, 1}});
  FakeFileOpener opener;
  opener.put("/Music/dhdr/01 Teil.ogg", std::vector<uint8_t>{'n', 'o', 'n', 'e'});

  LibraryIndex index = LibraryScanner::scan(lister, opener);

  TEST_ASSERT_EQUAL_STRING("dhdr", index.artists[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("dhdr", index.albums[0].title.c_str());
}

void test_files_outside_an_artist_album_layout_stay_unknown() {
  // Nothing above the file to borrow a name from.
  FakeFileLister lister({{"/loose.mp3", 10, 1}});
  FakeFileOpener opener;
  opener.put("/loose.mp3", std::vector<uint8_t>{'n', 'o', 't', 'a', 'g', 's'});

  LibraryIndex index = LibraryScanner::scan(lister, opener);

  TEST_ASSERT_EQUAL_STRING("Unknown Artist", index.artists[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Unknown Album", index.albums[0].title.c_str());
}

void test_artists_sorted_alphabetically_ignoring_case_the_and_accents() {
  // Listed in an order no reader would want them in.
  FakeFileLister lister({{"/1.mp3", 10, 1},
                         {"/2.mp3", 10, 2},
                         {"/3.mp3", 10, 3},
                         {"/4.mp3", 10, 4},
                         {"/5.mp3", 10, 5}});
  FakeFileOpener opener;
  opener.put("/1.mp3", buildTaggedMp3("Wir sind Helden", "A", "S"));
  opener.put("/2.mp3", buildTaggedMp3("The Beatles", "A", "S"));
  opener.put("/3.mp3", buildTaggedMp3("air", "A", "S"));
  opener.put("/4.mp3", buildTaggedMp3("\xC3\x84rzte", "A", "S"));
  opener.put("/5.mp3", buildTaggedMp3("Bj\xC3\xB6rk", "A", "S"));

  LibraryIndex index = LibraryScanner::scan(lister, opener);
  auto sorted = index.artistsSorted();

  TEST_ASSERT_EQUAL_UINT32(5, sorted.size());
  // "air" lowercase first, "Ärzte" like "Arzte" (with A, not after Z),
  // "The Beatles" under B, "Björk" like "Bjork". Only "The" is skipped:
  // "Die Ärzte" would belong under D.
  TEST_ASSERT_EQUAL_STRING("air", index.artists[sorted[0]].name.c_str());
  TEST_ASSERT_EQUAL_STRING("\xC3\x84rzte", index.artists[sorted[1]].name.c_str());
  TEST_ASSERT_EQUAL_STRING("The Beatles", index.artists[sorted[2]].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", index.artists[sorted[3]].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Wir sind Helden",
                            index.artists[sorted[4]].name.c_str());
}

void test_artist_named_only_the_keeps_its_name_as_key() {
  FakeFileLister lister({{"/1.mp3", 10, 1}, {"/2.mp3", 10, 2}});
  FakeFileOpener opener;
  opener.put("/1.mp3", buildTaggedMp3("Zebra", "A", "S"));
  opener.put("/2.mp3", buildTaggedMp3("The", "A", "S"));

  LibraryIndex index = LibraryScanner::scan(lister, opener);
  auto sorted = index.artistsSorted();

  // "The" alone has nothing left to sort by if the prefix is stripped, so
  // it keeps its full name and lands before "Zebra".
  TEST_ASSERT_EQUAL_STRING("The", index.artists[sorted[0]].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Zebra", index.artists[sorted[1]].name.c_str());
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

namespace {

class RecordingProgressListener : public knobify::library::ScanProgressListener {
 public:
  void onFileScanned(size_t) override {}
  void onNewAlbum(const std::string &albumFolderPath, knobify::library::RawFile &,
                   const TagResult &tags) override {
    calls.push_back(albumFolderPath);
    titlesOfFirstTrackPerCall.push_back(tags.title);
  }

  std::vector<std::string> calls;
  std::vector<std::string> titlesOfFirstTrackPerCall;
};

}  // namespace

void test_scanner_notifies_new_album_once_per_album_with_folder_path() {
  FakeFileLister lister({
      {"/Music/Artist One/Album One/a.mp3", 100, 1},
      {"/Music/Artist One/Album One/b.mp3", 200, 2},
      {"/Music/Artist Two/Album Two/c.mp3", 300, 3},
  });
  FakeFileOpener opener;
  opener.put("/Music/Artist One/Album One/a.mp3",
             buildTaggedMp3("Artist One", "Album One", "Track A"));
  opener.put("/Music/Artist One/Album One/b.mp3",
             buildTaggedMp3("Artist One", "Album One", "Track B"));
  opener.put("/Music/Artist Two/Album Two/c.mp3",
             buildTaggedMp3("Artist Two", "Album Two", "Track C"));

  RecordingProgressListener listener;
  LibraryScanner::scan(lister, opener, &listener);

  TEST_ASSERT_EQUAL_UINT32(2, listener.calls.size());
  TEST_ASSERT_EQUAL_STRING("/Music/Artist One/Album One",
                            listener.calls[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Track A",
                            listener.titlesOfFirstTrackPerCall[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/Music/Artist Two/Album Two",
                            listener.calls[1].c_str());
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
  index.tracks.push_back(Track{1, 0, "Track B", 2, "/b.mp3", 3});
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
  TEST_ASSERT_EQUAL_UINT16(3, decoded.tracks[1].discNumber);
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

// --- Collections (ADR 0018) ---

void test_two_roots_build_two_independent_indexes() {
  // Same scanner, two listers: nothing about a scan is tied to one root,
  // which is what lets three collections share this code.
  FakeFileLister music({{"/Music/Air/Moon Safari/02 Sexy Boy.mp3", 10, 1}});
  FakeFileLister books(
      {{"/Audiobooks/Douglas Adams/Hitchhiker/01 One.mp3", 10, 2}});
  FakeFileOpener opener;
  opener.put("/Music/Air/Moon Safari/02 Sexy Boy.mp3",
             std::vector<uint8_t>{'n', 'o', 't', 'a', 'g', 's'});
  opener.put("/Audiobooks/Douglas Adams/Hitchhiker/01 One.mp3",
             std::vector<uint8_t>{'n', 'o', 't', 'a', 'g', 's'});

  LibraryIndex musicIndex = LibraryScanner::scan(music, opener);
  LibraryIndex booksIndex = LibraryScanner::scan(books, opener);

  TEST_ASSERT_EQUAL(1, musicIndex.tracks.size());
  TEST_ASSERT_EQUAL(1, booksIndex.tracks.size());
  TEST_ASSERT_EQUAL_STRING("Air", musicIndex.artists[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Douglas Adams", booksIndex.artists[0].name.c_str());
  // Ids are indices into each index's own vectors, so the same id means
  // different things in each -- exactly why a screen carries its
  // collection alongside its ids.
  TEST_ASSERT_EQUAL(0, musicIndex.artists[0].id);
  TEST_ASSERT_EQUAL(0, booksIndex.artists[0].id);
}

void test_by_name_sorting_ignores_the_tag_folding_rules() {
  LibraryIndex index;
  index.artists = {{0, "The Beatles"}, {1, "\xC3\x84rzte"}, {2, "Zappa"}};

  // ByTag: "The " is stripped and the umlaut folded, so Ärzte -> "arzte",
  // The Beatles -> "beatles", Zappa -> "zappa".
  auto byTag = index.artistsSorted(knobify::library::SortOrder::ByTag);
  TEST_ASSERT_EQUAL(1, byTag[0]);
  TEST_ASSERT_EQUAL(0, byTag[1]);
  TEST_ASSERT_EQUAL(2, byTag[2]);

  // ByName: plain byte order, so "The Beatles" sorts under T and the
  // multi-byte umlaut sorts last -- a shelf order, not a tag order.
  auto byName = index.artistsSorted(knobify::library::SortOrder::ByName);
  TEST_ASSERT_EQUAL(0, byName[0]);
  TEST_ASSERT_EQUAL(2, byName[1]);
  TEST_ASSERT_EQUAL(1, byName[2]);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_scanner_groups_tagged_files_by_artist_and_album);
  RUN_TEST(test_scanner_falls_back_to_unknown_for_untagged_files);
  RUN_TEST(test_scanner_skips_unopenable_files);
  RUN_TEST(test_tracks_for_sorted_by_track_number);
  RUN_TEST(test_tracks_for_sorted_by_disc_then_track);
  RUN_TEST(test_tag_reader_falls_back_to_filename_disc_and_track);
  RUN_TEST(test_albums_for_sorted_by_year);
  RUN_TEST(test_untagged_files_fall_back_to_folder_names);
  RUN_TEST(test_two_roots_build_two_independent_indexes);
  RUN_TEST(test_by_name_sorting_ignores_the_tag_folding_rules);
  RUN_TEST(test_a_single_untagged_folder_names_both_artist_and_album);
  RUN_TEST(test_files_outside_an_artist_album_layout_stay_unknown);
  RUN_TEST(test_artists_sorted_alphabetically_ignoring_case_the_and_accents);
  RUN_TEST(test_artist_named_only_the_keeps_its_name_as_key);
  RUN_TEST(test_tag_reader_falls_back_to_filename_track_and_folder_year);
  RUN_TEST(test_scanner_notifies_new_album_once_per_album_with_folder_path);
  RUN_TEST(test_folder_browser_filters_and_sorts);
  RUN_TEST(test_signature_matches_for_identical_listing);
  RUN_TEST(test_signature_changes_when_a_file_changes);
  RUN_TEST(test_index_cache_round_trips);
  RUN_TEST(test_index_cache_rejects_corrupt_buffer);
  RUN_TEST(test_index_cache_rejects_truncated_buffer);
  return UNITY_END();
}
