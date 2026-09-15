#pragma once

#include <cstdint>
#include <string>

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
