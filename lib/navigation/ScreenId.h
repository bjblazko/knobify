#pragma once

#include <cstdint>
#include <string>

namespace knobify::navigation {

// Which screen is showing. Deliberately open-ended (not "Artists is the
// app root") so a future Home/menu screen can be inserted above today's
// roots without changing NavigationStack or TabController — see
// docs/adr/0004-navigation-library-and-index-architecture.md.
enum class ScreenKind {
  Artists,
  Albums,
  Tracks,
  Folder,
  NowPlaying,
};

// Parameters a screen needs to render itself. Only the fields relevant
// to a given ScreenKind are meaningful; unused fields are left at their
// default. A tagged union would avoid the unused-field waste, but at
// this scale (a handful of uint32_t ids and one path string) the extra
// complexity isn't worth it.
struct ScreenParams {
  uint32_t artistId = 0;
  uint32_t albumId = 0;
  std::string folderPath;
};

struct Screen {
  ScreenKind kind;
  ScreenParams params;
};

}  // namespace knobify::navigation
