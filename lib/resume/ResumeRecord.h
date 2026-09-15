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
// library index and change on a rescan.

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
  uint8_t lastMusicTab = 1;
  // Per tab, root first.
  std::array<std::vector<NavEntry>, kTabCount> stacks;

  bool operator==(const NavigationSnapshot &other) const {
    return activeTab == other.activeTab && lastMusicTab == other.lastMusicTab &&
           stacks == other.stacks;
  }
};

struct MusicSnapshot {
  uint8_t scope = 0;  // playback::PlayScope
  std::string trackPath;
  bool shuffle = false;
  // Driver byte offset and the readout's elapsed time, both approximate.
  uint32_t filePosition = 0;
  uint32_t elapsedSeconds = 0;

  bool operator==(const MusicSnapshot &other) const {
    return scope == other.scope && trackPath == other.trackPath &&
           shuffle == other.shuffle && filePosition == other.filePosition &&
           elapsedSeconds == other.elapsedSeconds;
  }
};

struct ResumeRecord {
  std::optional<NavigationSnapshot> navigation;
  std::optional<MusicSnapshot> music;

  bool operator==(const ResumeRecord &other) const {
    return navigation == other.navigation && music == other.music;
  }
  bool operator!=(const ResumeRecord &other) const { return !(*this == other); }
};

}  // namespace knobify::resume
