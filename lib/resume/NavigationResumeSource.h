#pragma once

#include <optional>
#include <string>

#include "LibraryScanner.h"
#include "PlaybackStateMachine.h"
#include "ResumeSource.h"
#include "TabController.h"

namespace knobify::resume {

// Resumes every tab's back-stack and which tab is active (ADR 0012), so the
// device comes back on the same screen with Back still leading where it did.
// Restore it after MusicResumeSource: Now Playing is dropped when no queue
// could be restored. A stack is cut at the first screen that no longer
// resolves (an album gone after a rescan) or doesn't belong in that tab.
class NavigationResumeSource : public ResumeSource {
 public:
  NavigationResumeSource(navigation::TabController &tabs,
                         const library::LibraryIndex &library,
                         const playback::PlaybackStateMachine &playback)
      : tabs_(tabs), library_(library), playback_(playback) {}

  void capture(ResumeRecord &record, uint32_t) override {
    NavigationSnapshot nav;
    nav.activeTab = static_cast<uint8_t>(tabs_.activeTab());
    nav.lastMusicTab = static_cast<uint8_t>(tabs_.lastMusicTab());
    for (std::size_t tab = 0; tab < NavigationSnapshot::kTabCount; ++tab) {
      const navigation::NavigationStack &stack =
          tabs_.stack(static_cast<navigation::Tab>(tab));
      for (std::size_t i = 0; i < stack.depth(); ++i) {
        nav.stacks[tab].push_back(entryFor(stack.at(i)));
      }
    }
    record.navigation = nav;
  }

  void restore(const ResumeRecord &record, uint32_t) override {
    if (!record.navigation) return;
    const NavigationSnapshot &nav = *record.navigation;
    for (std::size_t tab = 0; tab < NavigationSnapshot::kTabCount; ++tab) {
      auto tabId = static_cast<navigation::Tab>(tab);
      navigation::NavigationStack &stack = tabs_.stack(tabId);
      stack.popToRoot();
      const auto &entries = nav.stacks[tab];
      // Roots are fixed per tab; a saved root that differs means garbage.
      if (entries.empty() || entries[0].kind != static_cast<uint8_t>(stack.at(0).kind)) {
        continue;
      }
      for (std::size_t i = 1; i < entries.size(); ++i) {
        bool last = i + 1 == entries.size();
        std::optional<navigation::Screen> screen = resolve(entries[i]);
        if (!screen || !belongs(tabId, screen->kind, last)) break;
        stack.push(*screen);
      }
    }
    tabs_.restoreTabs(static_cast<navigation::Tab>(nav.activeTab),
                      static_cast<navigation::Tab>(nav.lastMusicTab));
  }

 private:
  using ScreenKind = navigation::ScreenKind;

  NavEntry entryFor(const navigation::Screen &screen) const {
    NavEntry entry;
    entry.kind = static_cast<uint8_t>(screen.kind);
    const auto &p = screen.params;
    if (screen.kind == ScreenKind::Albums && p.artistId < library_.artists.size()) {
      entry.key = library_.artists[p.artistId].name;
    } else if (screen.kind == ScreenKind::Tracks && p.albumId < library_.albums.size()) {
      const library::Album &album = library_.albums[p.albumId];
      if (album.artistId < library_.artists.size()) {
        entry.key = library_.artists[album.artistId].name;
      }
      entry.subKey = album.title;
    } else if (screen.kind == ScreenKind::Folder) {
      entry.key = p.folderPath;
    }
    return entry;
  }

  std::optional<navigation::Screen> resolve(const NavEntry &entry) const {
    if (entry.kind > static_cast<uint8_t>(ScreenKind::NowPlaying)) return std::nullopt;
    navigation::Screen screen{static_cast<ScreenKind>(entry.kind), {}};
    switch (screen.kind) {
      case ScreenKind::Albums: {
        auto artist = findArtist(entry.key);
        if (!artist) return std::nullopt;
        screen.params.artistId = *artist;
        return screen;
      }
      case ScreenKind::Tracks: {
        auto artist = findArtist(entry.key);
        if (!artist) return std::nullopt;
        for (const auto &album : library_.albums) {
          if (album.artistId == *artist && album.title == entry.subKey) {
            screen.params.albumId = album.id;
            return screen;
          }
        }
        return std::nullopt;
      }
      case ScreenKind::Folder:
        if (entry.key.empty() || entry.key[0] != '/') return std::nullopt;
        screen.params.folderPath = entry.key;
        return screen;
      default:
        return screen;
    }
  }

  std::optional<library::ArtistId> findArtist(const std::string &name) const {
    for (const auto &artist : library_.artists) {
      if (artist.name == name) return artist.id;
    }
    return std::nullopt;
  }

  // Which screens can sit above each tab's root. Now Playing only on top,
  // and only with something to show.
  bool belongs(navigation::Tab tab, ScreenKind kind, bool last) const {
    if (kind == ScreenKind::NowPlaying) return last && playback_.hasQueue();
    switch (tab) {
      case navigation::Tab::Menu:
        return kind == ScreenKind::Settings || kind == ScreenKind::Brightness;
      case navigation::Tab::Library:
        return kind == ScreenKind::Albums || kind == ScreenKind::Tracks;
      default:
        return kind == ScreenKind::Folder;
    }
  }

  navigation::TabController &tabs_;
  const library::LibraryIndex &library_;
  const playback::PlaybackStateMachine &playback_;
};

}  // namespace knobify::resume
