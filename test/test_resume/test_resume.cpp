#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "PlaybackResumeSource.h"
#include "NavigationResumeSource.h"
#include "ResumeCodec.h"
#include "ResumeScheduler.h"

using knobify::library::Album;
using knobify::library::Artist;
using knobify::library::LibraryIndex;
using knobify::library::Track;
using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;
using knobify::navigation::ScreenParams;
using knobify::navigation::Tab;
using knobify::navigation::TabController;
using knobify::playback::KeyValueStore;
using knobify::playback::PlaybackDriver;
using knobify::playback::PlaybackState;
using knobify::playback::PlaybackStateMachine;
using knobify::playback::PlayScope;
using knobify::playback::VolumePersistence;
using knobify::resume::BlobStore;
using knobify::resume::PlaybackResumeSource;
using knobify::resume::PlaybackSnapshot;
using knobify::resume::NavEntry;
using knobify::resume::NavigationResumeSource;
using knobify::resume::NavigationSnapshot;
using knobify::resume::ResumeCodec;
using knobify::resume::ResumeRecord;
using knobify::resume::ResumeScheduler;
using knobify::resume::ResumeSource;

void setUp() {}
void tearDown() {}

namespace {

class FakeDriver : public PlaybackDriver {
 public:
  bool playFile(const std::string &path) override { return playFileAt(path, 0); }
  bool playFileAt(const std::string &path, uint32_t position) override {
    lastPlayed = path;
    lastPosition = position;
    playCount++;
    return true;
  }
  uint32_t filePosition() override { return position; }
  bool seekByMs(int32_t) override { return true; }
  void pause() override {}
  void resume() override {}
  void stop() override {}
  void setVolume(uint8_t) override {}
  void setOutputGain(uint16_t) override {}
  bool isRunning() override { return true; }
  uint32_t durationSeconds() override { return 0; }
  knobify::playback::SampleWindow readRecentSamples(int16_t *, size_t) override {
    return {};
  }
  void loop() override {}

  std::string lastPlayed;
  uint32_t lastPosition = 0;
  uint32_t position = 0;
  int playCount = 0;
};

class FakeStore : public KeyValueStore, public BlobStore {
 public:
  bool getU8(const std::string &, uint8_t &) override { return false; }
  void setU8(const std::string &, uint8_t) override {}
  bool getBlob(const std::string &key, std::vector<uint8_t> &out) override {
    auto it = blobs.find(key);
    out = it == blobs.end() ? std::vector<uint8_t>{} : it->second;
    return !out.empty();
  }
  bool setBlob(const std::string &key, const std::vector<uint8_t> &data) override {
    blobs[key] = data;
    writes++;
    return true;
  }
  void removeBlob(const std::string &key) override { blobs.erase(key); }

  std::map<std::string, std::vector<uint8_t>> blobs;
  int writes = 0;
};

// Two artists; "B" has one album with two tracks, "A" one with one track.
LibraryIndex makeLibrary() {
  LibraryIndex index;
  index.artists = {Artist{0, "A"}, Artist{1, "B"}};
  index.albums = {Album{0, 0, "First", 2001}, Album{1, 1, "Second", 2002}};
  index.tracks = {Track{0, 0, "a1", 1, "/Music/A/First/a1.mp3"},
                  Track{1, 1, "b1", 1, "/Music/B/Second/b1.mp3"},
                  Track{2, 1, "b2", 2, "/Music/B/Second/b2.mp3"}};
  return index;
}

// The three collections, with only Music populated -- the resume sources
// resolve names against whichever one a record names (ADR 0018).
class FakeCollections : public knobify::collection::CollectionSet {
 public:
  LibraryIndex &index(knobify::collection::CollectionId id) override {
    return indexes[knobify::collection::indexOf(id)];
  }

  void rescan(knobify::collection::CollectionId,
              knobify::library::ScanProgressListener *) override {
    ++rescans;
  }

  LibraryIndex indexes[knobify::collection::kCollectionCount];
  int rescans = 0;
};

bool alwaysExists(const std::string &) { return true; }
bool neverExists(const std::string &) { return false; }

ResumeRecord sampleRecord() {
  ResumeRecord record;
  NavigationSnapshot nav;
  nav.activeTab = 1;
  nav.lastBrowseTab = 1;
  nav.stacks[0] = {NavEntry{static_cast<uint8_t>(ScreenKind::Home), "", ""}};
  nav.stacks[1] = {NavEntry{static_cast<uint8_t>(ScreenKind::Artists), "", ""},
                   NavEntry{static_cast<uint8_t>(ScreenKind::Albums), "B", ""},
                   NavEntry{static_cast<uint8_t>(ScreenKind::Tracks), "B", "Second"},
                   NavEntry{static_cast<uint8_t>(ScreenKind::NowPlaying), "", ""}};
  nav.stacks[2] = {NavEntry{static_cast<uint8_t>(ScreenKind::Folder), "/Music", ""}};
  record.navigation = nav;
  PlaybackSnapshot music;
  music.scope = static_cast<uint8_t>(PlayScope::Album);
  music.trackPath = "/Music/B/Second/b2.mp3";
  music.shuffle = true;
  music.filePosition = 123456;
  music.elapsedSeconds = 95;
  record.music = music;
  return record;
}

// Recomputes the header CRC after a payload edit, so a test reaches the
// checks behind it.
void fixCrc(std::vector<uint8_t> &bytes) {
  uint32_t crc = ResumeCodec::crc32(bytes.data() + 11, bytes.size() - 11);
  for (int i = 0; i < 4; ++i) bytes[7 + i] = static_cast<uint8_t>(crc >> (8 * i));
}

struct Rig {
  Rig() { collections.indexes[0] = makeLibrary(); }

  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume{store};
  PlaybackStateMachine playback{driver, volume};
  FakeCollections collections;
  TabController tabs;
  PlaybackResumeSource music{playback, collections, &alwaysExists};
  NavigationResumeSource navigation{tabs, collections, playback};
  // Most tests only look at Music's index.
  LibraryIndex &library = collections.indexes[0];
};

}  // namespace

// --- Codec ---

void test_codec_round_trips_a_full_record() {
  ResumeRecord record = sampleRecord();
  ResumeRecord decoded;
  TEST_ASSERT_TRUE(ResumeCodec::decode(ResumeCodec::encode(record), decoded));
  TEST_ASSERT_TRUE(decoded == record);
}

void test_codec_round_trips_an_empty_record() {
  ResumeRecord decoded = sampleRecord();
  TEST_ASSERT_TRUE(ResumeCodec::decode(ResumeCodec::encode(ResumeRecord{}), decoded));
  TEST_ASSERT_FALSE(decoded.navigation.has_value());
  TEST_ASSERT_FALSE(decoded.music.has_value());
}

void test_codec_rejects_every_truncation() {
  std::vector<uint8_t> bytes = ResumeCodec::encode(sampleRecord());
  for (size_t length = 0; length < bytes.size(); ++length) {
    ResumeRecord decoded;
    std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + length);
    TEST_ASSERT_FALSE(ResumeCodec::decode(cut, decoded));
    TEST_ASSERT_FALSE(decoded.music.has_value());
  }
}

void test_codec_rejects_every_single_bit_flip() {
  std::vector<uint8_t> bytes = ResumeCodec::encode(sampleRecord());
  for (size_t i = 0; i < bytes.size(); ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      std::vector<uint8_t> flipped = bytes;
      flipped[i] ^= static_cast<uint8_t>(1 << bit);
      ResumeRecord decoded;
      TEST_ASSERT_FALSE(ResumeCodec::decode(flipped, decoded));
    }
  }
}

void test_codec_rejects_trailing_garbage_and_other_versions() {
  std::vector<uint8_t> bytes = ResumeCodec::encode(sampleRecord());
  ResumeRecord decoded;
  std::vector<uint8_t> longer = bytes;
  longer.push_back(0);
  TEST_ASSERT_FALSE(ResumeCodec::decode(longer, decoded));
  std::vector<uint8_t> future = bytes;
  future[4] = ResumeCodec::kVersion + 1;
  TEST_ASSERT_FALSE(ResumeCodec::decode(future, decoded));
}

void test_codec_skips_unknown_sections() {
  std::vector<uint8_t> bytes = ResumeCodec::encode(sampleRecord());
  // A future section: tag 200, three bytes of body.
  bytes.insert(bytes.end(), {200, 3, 0, 9, 9, 9});
  uint16_t payload = static_cast<uint16_t>(bytes.size() - 11);
  bytes[5] = static_cast<uint8_t>(payload);
  bytes[6] = static_cast<uint8_t>(payload >> 8);
  fixCrc(bytes);
  ResumeRecord decoded;
  TEST_ASSERT_TRUE(ResumeCodec::decode(bytes, decoded));
  TEST_ASSERT_TRUE(decoded == sampleRecord());
}

void test_codec_rejects_out_of_range_values_even_with_valid_crc() {
  ResumeRecord record;
  record.navigation = NavigationSnapshot{};
  std::vector<uint8_t> bytes = ResumeCodec::encode(record);
  bytes[14] = 7;  // activeTab, after header (11) and section header (3).
  fixCrc(bytes);
  ResumeRecord decoded;
  TEST_ASSERT_FALSE(ResumeCodec::decode(bytes, decoded));
}

void test_codec_refuses_to_encode_oversized_strings() {
  ResumeRecord record;
  record.music = PlaybackSnapshot{};
  record.music->trackPath = std::string(ResumeCodec::kMaxStringLength + 1, 'x');
  TEST_ASSERT_TRUE(ResumeCodec::encode(record).empty());
}

// --- Scheduler ---

namespace {

class FakeSource : public ResumeSource {
 public:
  void capture(ResumeRecord &record, uint32_t) override { record = state; }
  void restore(const ResumeRecord &record, uint32_t) override {
    restored = record;
    restoreCalls++;
  }
  ResumeRecord state;
  ResumeRecord restored;
  int restoreCalls = 0;
};

ResumeRecord musicAt(const std::string &path, uint32_t seconds) {
  ResumeRecord record;
  record.music = PlaybackSnapshot{};
  record.music->trackPath = path;
  record.music->elapsedSeconds = seconds;
  record.music->filePosition = seconds * 1000;
  return record;
}

}  // namespace

void test_scheduler_saves_a_structural_change_after_it_settles() {
  FakeStore store;
  FakeSource source;
  ResumeScheduler scheduler(store, {&source});
  scheduler.restore(0);
  source.state = musicAt("/a.mp3", 0);
  scheduler.tick(0);
  scheduler.tick(ResumeScheduler::kStructureDelayMs - 1000);
  TEST_ASSERT_EQUAL(0, store.writes);
  scheduler.tick(ResumeScheduler::kStructureDelayMs);
  TEST_ASSERT_EQUAL(1, store.writes);
  ResumeRecord saved;
  TEST_ASSERT_TRUE(ResumeCodec::decode(store.blobs[ResumeScheduler::kKey], saved));
  TEST_ASSERT_TRUE(saved == source.state);
}

void test_scheduler_waits_while_structure_keeps_changing() {
  FakeStore store;
  FakeSource source;
  ResumeScheduler scheduler(store, {&source});
  scheduler.restore(0);
  for (uint32_t t = 0; t < 10; ++t) {
    source.state = musicAt("/" + std::to_string(t) + ".mp3", 0);
    scheduler.tick(t * 1000);
  }
  TEST_ASSERT_EQUAL(0, store.writes);
  scheduler.tick(9000 + ResumeScheduler::kStructureDelayMs);
  TEST_ASSERT_EQUAL(1, store.writes);
}

void test_scheduler_saves_position_only_changes_at_the_slow_interval() {
  FakeStore store;
  FakeSource source;
  ResumeScheduler scheduler(store, {&source});
  scheduler.restore(0);
  source.state = musicAt("/a.mp3", 0);
  scheduler.tick(0);
  scheduler.tick(ResumeScheduler::kStructureDelayMs);
  TEST_ASSERT_EQUAL(1, store.writes);
  uint32_t savedAt = ResumeScheduler::kStructureDelayMs;
  for (uint32_t t = savedAt + 1000; t < savedAt + ResumeScheduler::kPositionIntervalMs;
       t += 1000) {
    source.state = musicAt("/a.mp3", t / 1000);
    scheduler.tick(t);
  }
  TEST_ASSERT_EQUAL(1, store.writes);
  source.state = musicAt("/a.mp3", 40);
  scheduler.tick(savedAt + ResumeScheduler::kPositionIntervalMs);
  TEST_ASSERT_EQUAL(2, store.writes);
}

void test_scheduler_save_now_writes_immediately_and_only_when_changed() {
  FakeStore store;
  FakeSource source;
  ResumeScheduler scheduler(store, {&source});
  scheduler.restore(0);
  source.state = musicAt("/a.mp3", 7);
  scheduler.tick(500);  // Captured, but the structure hasn't settled.
  TEST_ASSERT_EQUAL(0, store.writes);
  scheduler.saveNow(600);  // Within the capture interval, too.
  TEST_ASSERT_EQUAL(1, store.writes);
  ResumeRecord saved;
  TEST_ASSERT_TRUE(ResumeCodec::decode(store.blobs[ResumeScheduler::kKey], saved));
  TEST_ASSERT_TRUE(saved == source.state);
  scheduler.saveNow(700);
  TEST_ASSERT_EQUAL(1, store.writes);
}

void test_scheduler_never_writes_while_nothing_changes() {
  FakeStore store;
  FakeSource source;
  ResumeScheduler scheduler(store, {&source});
  scheduler.restore(0);
  source.state = musicAt("/a.mp3", 12);
  for (uint32_t t = 0; t <= 60 * 60 * 1000u; t += 1000) scheduler.tick(t);
  TEST_ASSERT_EQUAL(1, store.writes);
}

void test_scheduler_restores_and_does_not_rewrite_the_same_state() {
  FakeStore store;
  store.blobs[ResumeScheduler::kKey] = ResumeCodec::encode(musicAt("/a.mp3", 7));
  FakeSource source;
  ResumeScheduler scheduler(store, {&source});
  TEST_ASSERT_TRUE(scheduler.restore(100));
  TEST_ASSERT_EQUAL(1, source.restoreCalls);
  TEST_ASSERT_TRUE(source.restored == musicAt("/a.mp3", 7));
  source.state = musicAt("/a.mp3", 7);
  for (uint32_t t = 100; t < 120000; t += 1000) scheduler.tick(t);
  TEST_ASSERT_EQUAL(0, store.writes);
}

void test_scheduler_ignores_a_corrupt_record() {
  FakeStore store;
  store.blobs[ResumeScheduler::kKey] = {'K', 'R', 'E', 'S', 1, 0};
  FakeSource source;
  ResumeScheduler scheduler(store, {&source});
  TEST_ASSERT_FALSE(scheduler.restore(0));
  TEST_ASSERT_EQUAL(0, source.restoreCalls);
}

void test_scheduler_discard_removes_the_record() {
  FakeStore store;
  store.blobs[ResumeScheduler::kKey] = ResumeCodec::encode(musicAt("/a.mp3", 7));
  FakeSource source;
  ResumeScheduler scheduler(store, {&source});
  scheduler.discard(0);
  TEST_ASSERT_EQUAL(0u, store.blobs.count(ResumeScheduler::kKey));
}

// --- Sources ---

void test_capture_then_restore_brings_back_screen_and_paused_track() {
  Rig before;
  before.tabs.openCollection(knobify::collection::CollectionId::Music);
  before.tabs.activeStack().push(Screen{ScreenKind::Albums, ScreenParams{.artistId = 1}});
  before.tabs.activeStack().push(Screen{ScreenKind::Tracks, ScreenParams{.albumId = 1}});
  before.playback.play({"/Music/B/Second/b1.mp3", "/Music/B/Second/b2.mp3"}, 1, 0,
                       false, PlayScope::Album);
  before.tabs.activeStack().push(Screen{ScreenKind::NowPlaying, {}});
  before.driver.position = 5000;
  ResumeRecord record;
  before.music.capture(record, 42000);
  before.navigation.capture(record, 42000);

  Rig after;
  after.music.restore(record, 500);
  after.navigation.restore(record, 500);

  TEST_ASSERT_TRUE(after.tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(after.tabs.activeStack().current().kind == ScreenKind::NowPlaying);
  TEST_ASSERT_EQUAL(4u, after.tabs.activeStack().depth());
  TEST_ASSERT_EQUAL(1u, after.tabs.activeStack().at(2).params.albumId);
  TEST_ASSERT_TRUE(after.playback.state() == PlaybackState::Paused);
  TEST_ASSERT_EQUAL_STRING("/Music/B/Second/b2.mp3", after.playback.currentPath().c_str());
  TEST_ASSERT_EQUAL(42000u, after.playback.elapsedMs(90000));
  TEST_ASSERT_TRUE(after.playback.scope() == PlayScope::Album);
  // Nothing auto-plays; play resumes at the saved position.
  TEST_ASSERT_EQUAL(0, after.driver.playCount);
  after.playback.togglePlayPause(1000);
  TEST_ASSERT_EQUAL(1, after.driver.playCount);
  TEST_ASSERT_EQUAL(5000u, after.driver.lastPosition);
  TEST_ASSERT_TRUE(after.playback.state() == PlaybackState::Playing);
}

void test_restore_resolves_names_to_new_ids_after_a_rescan() {
  ResumeRecord record = sampleRecord();
  Rig rig;
  // A rescan put artist "B" first.
  rig.library.artists = {Artist{0, "B"}, Artist{1, "A"}};
  rig.library.albums = {Album{0, 0, "Second", 2002}, Album{1, 1, "First", 2001}};
  rig.library.tracks = {Track{0, 0, "b1", 1, "/Music/B/Second/b1.mp3"},
                        Track{1, 0, "b2", 2, "/Music/B/Second/b2.mp3"},
                        Track{2, 1, "a1", 1, "/Music/A/First/a1.mp3"}};
  rig.music.restore(record, 0);
  rig.navigation.restore(record, 0);
  const auto &stack = rig.tabs.stack(Tab::Library);
  TEST_ASSERT_EQUAL(4u, stack.depth());
  TEST_ASSERT_EQUAL(0u, stack.at(1).params.artistId);
  TEST_ASSERT_EQUAL(0u, stack.at(2).params.albumId);
  TEST_ASSERT_EQUAL_STRING("/Music/B/Second/b2.mp3", rig.playback.currentPath().c_str());
}

void test_restore_cuts_the_stack_at_a_vanished_album_and_drops_now_playing() {
  ResumeRecord record = sampleRecord();
  Rig rig;
  rig.library.albums[1].title = "Renamed";
  rig.library.tracks.erase(rig.library.tracks.begin() + 2);  // b2 is gone.
  rig.music.restore(record, 0);
  rig.navigation.restore(record, 0);
  TEST_ASSERT_FALSE(rig.playback.hasQueue());
  TEST_ASSERT_TRUE(rig.tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(rig.tabs.activeStack().current().kind == ScreenKind::Albums);
}

void test_restore_rejects_screens_in_the_wrong_tab() {
  ResumeRecord record = sampleRecord();
  record.navigation->stacks[0].push_back(
      NavEntry{static_cast<uint8_t>(ScreenKind::Tracks), "B", "Second"});
  record.navigation->stacks[1].insert(record.navigation->stacks[1].begin() + 1,
                                      NavEntry{99, "", ""});
  Rig rig;
  rig.navigation.restore(record, 0);
  TEST_ASSERT_EQUAL(1u, rig.tabs.stack(Tab::Menu).depth());
  TEST_ASSERT_EQUAL(1u, rig.tabs.stack(Tab::Library).depth());
}

void test_restore_of_a_missing_file_leaves_no_queue() {
  ResumeRecord record;
  record.music = PlaybackSnapshot{};
  record.music->scope = static_cast<uint8_t>(PlayScope::File);
  record.music->trackPath = "/Music/loose.mp3";
  Rig rig;
  PlaybackResumeSource missing(rig.playback, rig.collections, &neverExists);
  missing.restore(record, 0);
  TEST_ASSERT_FALSE(rig.playback.hasQueue());
  rig.music.restore(record, 0);
  TEST_ASSERT_TRUE(rig.playback.hasQueue());
}

void test_skipping_while_cued_starts_the_next_track_from_the_beginning() {
  Rig rig;
  rig.playback.cue({"/a.mp3", "/b.mp3"}, 0, false, PlayScope::Album, 9999, 60, 0);
  rig.playback.next(1000);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", rig.driver.lastPlayed.c_str());
  TEST_ASSERT_EQUAL(0u, rig.driver.lastPosition);
  TEST_ASSERT_EQUAL(0u, rig.playback.elapsedMs(1000));
  TEST_ASSERT_TRUE(rig.playback.state() == PlaybackState::Playing);
}

void test_cued_shuffle_keeps_the_saved_track_current() {
  Rig rig;
  rig.playback.cue({"/a.mp3", "/b.mp3", "/c.mp3", "/d.mp3"}, 2, true,
                   PlayScope::Library, 0, 0, 0);
  TEST_ASSERT_TRUE(rig.playback.shuffle());
  TEST_ASSERT_EQUAL_STRING("/c.mp3", rig.playback.currentPath().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_codec_round_trips_a_full_record);
  RUN_TEST(test_codec_round_trips_an_empty_record);
  RUN_TEST(test_codec_rejects_every_truncation);
  RUN_TEST(test_codec_rejects_every_single_bit_flip);
  RUN_TEST(test_codec_rejects_trailing_garbage_and_other_versions);
  RUN_TEST(test_codec_skips_unknown_sections);
  RUN_TEST(test_codec_rejects_out_of_range_values_even_with_valid_crc);
  RUN_TEST(test_codec_refuses_to_encode_oversized_strings);
  RUN_TEST(test_scheduler_saves_a_structural_change_after_it_settles);
  RUN_TEST(test_scheduler_waits_while_structure_keeps_changing);
  RUN_TEST(test_scheduler_saves_position_only_changes_at_the_slow_interval);
  RUN_TEST(test_scheduler_save_now_writes_immediately_and_only_when_changed);
  RUN_TEST(test_scheduler_never_writes_while_nothing_changes);
  RUN_TEST(test_scheduler_restores_and_does_not_rewrite_the_same_state);
  RUN_TEST(test_scheduler_ignores_a_corrupt_record);
  RUN_TEST(test_scheduler_discard_removes_the_record);
  RUN_TEST(test_capture_then_restore_brings_back_screen_and_paused_track);
  RUN_TEST(test_restore_resolves_names_to_new_ids_after_a_rescan);
  RUN_TEST(test_restore_cuts_the_stack_at_a_vanished_album_and_drops_now_playing);
  RUN_TEST(test_restore_rejects_screens_in_the_wrong_tab);
  RUN_TEST(test_restore_of_a_missing_file_leaves_no_queue);
  RUN_TEST(test_skipping_while_cued_starts_the_next_track_from_the_beginning);
  RUN_TEST(test_cued_shuffle_keeps_the_saved_track_current);
  return UNITY_END();
}
