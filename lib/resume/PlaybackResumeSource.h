#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "CollectionSet.h"
#include "PlaybackStateMachine.h"
#include "PlaylistBuilder.h"
#include "ResumeSource.h"

namespace knobify::resume {

// Resumes the play queue (ADR 0012). Only the current track's path, the
// scope and the collection it came from are saved, not the queue: the queue
// is rebuilt from that collection's index the same way a tap builds it, so a
// rescan in between can't leave stale paths. With shuffle on, the rest of
// the queue is reshuffled after the track.
class PlaybackResumeSource : public ResumeSource {
 public:
  // `fileExists` checks a Files-tab path still exists (SD on the device).
  using FileExists = bool (*)(const std::string &path);

  PlaybackResumeSource(playback::PlaybackStateMachine &playback,
                       const collection::CollectionSet &collections,
                       FileExists fileExists)
      : playback_(playback),
        collections_(collections),
        fileExists_(fileExists) {}

  void capture(ResumeRecord &record, uint32_t nowMs) override {
    if (!playback_.hasQueue()) {
      record.music.reset();
      return;
    }
    PlaybackSnapshot music;
    music.scope = static_cast<uint8_t>(playback_.scope());
    music.trackPath = playback_.currentPath();
    // Derived from the track's own path rather than tracked alongside
    // playback: nothing can then forget to update it.
    collection::CollectionId playing = collection::CollectionId::Music;
    collections_.findByPath(music.trackPath, playing);
    music.collection = static_cast<uint8_t>(playing);
    music.shuffle = playback_.shuffle();
    music.filePosition = playback_.filePosition();
    music.elapsedSeconds = playback_.elapsedMs(nowMs) / 1000;
    record.music = music;
  }

  void restore(const ResumeRecord &record, uint32_t nowMs) override {
    if (!record.music) return;
    const PlaybackSnapshot &music = *record.music;
    if (music.scope > static_cast<uint8_t>(playback::PlayScope::Library)) return;
    if (!collection::isValidCollection(music.collection)) return;
    auto scope = static_cast<playback::PlayScope>(music.scope);
    auto collectionId = static_cast<collection::CollectionId>(music.collection);

    std::vector<std::string> playlist =
        playlistFor(collectionId, scope, music.trackPath);
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

  std::vector<std::string> playlistFor(collection::CollectionId collectionId,
                                       playback::PlayScope scope,
                                       const std::string &path) const {
    using library::PlaylistBuilder;
    if (scope == playback::PlayScope::File) {
      if (path.empty() || !fileExists_(path)) return {};
      return {path};
    }
    const library::LibraryIndex &library = collections_.index(collectionId);
    const library::SortOrder order = collections_.profile(collectionId).sort;
    auto track = std::find_if(library.tracks.begin(), library.tracks.end(),
                              [&](const library::Track &t) { return t.filePath == path; });
    if (track == library.tracks.end() || track->albumId >= library.albums.size()) {
      return {};
    }
    const library::Album &album = library.albums[track->albumId];
    switch (scope) {
      case playback::PlayScope::Album:
        return PlaylistBuilder::forAlbum(library, album.id);
      case playback::PlayScope::Artist:
        return PlaylistBuilder::forArtist(library, order, album.artistId);
      default:
        return PlaylistBuilder::forLibrary(library, order);
    }
  }

  playback::PlaybackStateMachine &playback_;
  const collection::CollectionSet &collections_;
  FileExists fileExists_;
};

}  // namespace knobify::resume
