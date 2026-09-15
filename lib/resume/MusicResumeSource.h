#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "LibraryScanner.h"
#include "PlaybackStateMachine.h"
#include "PlaylistBuilder.h"
#include "ResumeSource.h"

namespace knobify::resume {

// Resumes the music queue (ADR 0012). Only the current track's path and the
// scope are saved, not the queue: the queue is rebuilt from the library the
// same way a tap builds it, so a rescan in between can't leave stale paths.
// With shuffle on, the rest of the queue is reshuffled after the track.
class MusicResumeSource : public ResumeSource {
 public:
  // `fileExists` checks a Files-tab path still exists (SD on the device).
  using FileExists = bool (*)(const std::string &path);

  MusicResumeSource(playback::PlaybackStateMachine &playback,
                    const library::LibraryIndex &library, FileExists fileExists)
      : playback_(playback), library_(library), fileExists_(fileExists) {}

  void capture(ResumeRecord &record, uint32_t nowMs) override {
    if (!playback_.hasQueue()) {
      record.music.reset();
      return;
    }
    MusicSnapshot music;
    music.scope = static_cast<uint8_t>(playback_.scope());
    music.trackPath = playback_.currentPath();
    music.shuffle = playback_.shuffle();
    music.filePosition = playback_.filePosition();
    music.elapsedSeconds = playback_.elapsedMs(nowMs) / 1000;
    record.music = music;
  }

  void restore(const ResumeRecord &record, uint32_t nowMs) override {
    if (!record.music) return;
    const MusicSnapshot &music = *record.music;
    if (music.scope > static_cast<uint8_t>(playback::PlayScope::Library)) return;
    auto scope = static_cast<playback::PlayScope>(music.scope);

    std::vector<std::string> playlist = playlistFor(scope, music.trackPath);
    auto it = std::find(playlist.begin(), playlist.end(), music.trackPath);
    if (it == playlist.end()) return;
    auto index = static_cast<size_t>(it - playlist.begin());
    // A day-long "elapsed" is corrupt, not a real position.
    bool plausible = music.elapsedSeconds < kMaxElapsedSeconds;
    playback_.cue(std::move(playlist), index, music.shuffle,
                  scope, plausible ? music.filePosition : 0,
                  plausible ? music.elapsedSeconds : 0, nowMs);
  }

 private:
  static constexpr uint32_t kMaxElapsedSeconds = 24 * 60 * 60;

  std::vector<std::string> playlistFor(playback::PlayScope scope,
                                       const std::string &path) const {
    using library::PlaylistBuilder;
    if (scope == playback::PlayScope::File) {
      if (path.empty() || !fileExists_(path)) return {};
      return {path};
    }
    auto track = std::find_if(library_.tracks.begin(), library_.tracks.end(),
                              [&](const library::Track &t) { return t.filePath == path; });
    if (track == library_.tracks.end() || track->albumId >= library_.albums.size()) {
      return {};
    }
    const library::Album &album = library_.albums[track->albumId];
    switch (scope) {
      case playback::PlayScope::Album:
        return PlaylistBuilder::forAlbum(library_, album.id);
      case playback::PlayScope::Artist:
        return PlaylistBuilder::forArtist(library_, album.artistId);
      default:
        return PlaylistBuilder::forLibrary(library_);
    }
  }

  playback::PlaybackStateMachine &playback_;
  const library::LibraryIndex &library_;
  FileExists fileExists_;
};

}  // namespace knobify::resume
