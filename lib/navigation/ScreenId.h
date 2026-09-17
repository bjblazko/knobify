#pragma once

#include <cstdint>
#include <string>

#include "CollectionId.h"

namespace knobify::navigation {

// Which screen is showing. Deliberately open-ended (not "Artists is the
// app root"), which is what let the Home menu be added above the music
// roots later -- see docs/adr/0004-navigation-library-and-index-architecture.md
// and docs/adr/0010-main-menu-and-settings.md.
enum class ScreenKind {
  Home,
  Settings,
  Brightness,
  Artists,
  Albums,
  Tracks,
  Folder,
  NowPlaying,
  // Appended, not next to Brightness: resume records store kinds by value
  // (NavigationResumeSource), and a calibration in progress is never
  // resumed -- anything past NowPlaying is dropped on restore.
  TouchCalibration,
  // Past NowPlaying too: a sleep timer is never set after a reboot, so its
  // screen isn't restored either (ADR 0015).
  SleepTimer,
  // Past NowPlaying: a USB drive session ends with the power (ADR 0016).
  UsbDrive,
  // Past NowPlaying too (ADR 0018): both are Settings sub-screens, and a
  // resume record that lands mid-configuration is not worth restoring.
  // Appended rather than placed next to Settings for the same reason
  // TouchCalibration was -- resume records store kinds by value.
  RescanPicker,
  MenuVisibility,
  // The Music browse axes (ADR 0021), appended for the same reason as
  // everything above: resume records store kinds by value. One screen
  // covers every album shelf -- all albums, one year's, one genre's --
  // since only the filter in ScreenParams differs.
  AlbumsFlat,
  Songs,
  Years,
  Genres,
  BrowseAxis,
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
  // AlbumsFlat's filter: a year, a genre, or neither (every album).
  // Year 0 and genre 0 both mean "unset" in the index too, so no extra
  // flag is needed to say which shelf is being shown.
  uint16_t year = 0;
  uint32_t genreId = 0;
  // Which collection's index the ids above are indices into, and whose
  // root a Folder path sits under (ADR 0018). Every push carries the
  // pushing screen's value along, so a whole browse stack stays inside
  // one collection.
  collection::CollectionId collection = collection::CollectionId::Music;
};

struct Screen {
  ScreenKind kind;
  ScreenParams params;
};

}  // namespace knobify::navigation
