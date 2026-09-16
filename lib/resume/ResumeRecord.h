#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace knobify::resume {

// Where the device was, persisted periodically so a power cut resumes there
// -- see docs/adr/0012-resume-session.md. Each part is optional and stored as
// its own tagged section, so a future source (podcasts, web radio, video)
// adds a section without touching the others.
//
// Library screens are identified by names, not ids: ids are indices into the
// collection's index and change on a rescan.

// Mirrors navigation::Tab and ScreenKind by value; the codec only checks
// ranges, NavigationResumeSource checks what makes sense where.
struct NavEntry {
  uint8_t kind = 0;
  // Albums: artist name. Tracks: artist name + album title. Folder: path.
  std::string key;
  std::string subKey;

  bool operator==(const NavEntry &other) const {
    return kind == other.kind && key == other.key && subKey == other.subKey;
  }
};

struct NavigationSnapshot {
  static constexpr std::size_t kTabCount = 3;
  static constexpr std::size_t kMaxDepth = 8;

  uint8_t activeTab = 0;
  uint8_t lastBrowseTab = 1;
  // Which collection the browse stacks below belong to
  // (collection::CollectionId). Only the active one is saved: three
  // collections' worth of stacks would not fit kMaxEncodedSize, and coming
  // back to the shelf you left is what matters (ADR 0018).
  uint8_t collection = 0;
  // Per tab, root first.
  std::array<std::vector<NavEntry>, kTabCount> stacks;

  bool operator==(const NavigationSnapshot &other) const {
    return activeTab == other.activeTab &&
           lastBrowseTab == other.lastBrowseTab &&
           collection == other.collection && stacks == other.stacks;
  }
};

// What was playing. There is only ever one player, so there is only ever
// one of these -- `collection` says which shelf the queue was built from,
// not that several are playing at once.
struct PlaybackSnapshot {
  uint8_t scope = 0;  // playback::PlayScope
  std::string trackPath;
  bool shuffle = false;
  // Driver byte offset and the readout's elapsed time, both approximate.
  uint32_t filePosition = 0;
  uint32_t elapsedSeconds = 0;
  uint8_t collection = 0;  // collection::CollectionId

  bool operator==(const PlaybackSnapshot &other) const {
    return scope == other.scope && trackPath == other.trackPath &&
           shuffle == other.shuffle && filePosition == other.filePosition &&
           elapsedSeconds == other.elapsedSeconds &&
           collection == other.collection;
  }
};

struct ResumeRecord {
  std::optional<NavigationSnapshot> navigation;
  std::optional<PlaybackSnapshot> music;

  bool operator==(const ResumeRecord &other) const {
    return navigation == other.navigation && music == other.music;
  }
  bool operator!=(const ResumeRecord &other) const { return !(*this == other); }
};

}  // namespace knobify::resume
