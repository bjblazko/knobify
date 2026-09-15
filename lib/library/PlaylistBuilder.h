#pragma once

#include <string>
#include <vector>

#include "LibraryScanner.h"

namespace knobify::library {

// File paths for each shuffle/playback scope (ADR 0011), in the same order
// the list screens show them. Relies on ids being indices into the index's
// vectors, as LibraryScanner assigns them (and albumsFor()/tracksFor()
// already assume).
class PlaylistBuilder {
 public:
  static std::vector<std::string> forAlbum(const LibraryIndex &index,
                                           AlbumId albumId) {
    std::vector<std::string> paths;
    appendAlbum(index, albumId, paths);
    return paths;
  }

  static std::vector<std::string> forArtist(const LibraryIndex &index,
                                            ArtistId artistId) {
    std::vector<std::string> paths;
    appendArtist(index, artistId, paths);
    return paths;
  }

  static std::vector<std::string> forLibrary(const LibraryIndex &index) {
    std::vector<std::string> paths;
    paths.reserve(index.tracks.size());
    for (const auto &artist : index.artists) appendArtist(index, artist.id, paths);
    return paths;
  }

 private:
  static void appendAlbum(const LibraryIndex &index, AlbumId albumId,
                          std::vector<std::string> &paths) {
    for (TrackId id : index.tracksFor(albumId)) {
      paths.push_back(index.tracks[id].filePath);
    }
  }

  static void appendArtist(const LibraryIndex &index, ArtistId artistId,
                           std::vector<std::string> &paths) {
    for (AlbumId id : index.albumsFor(artistId)) appendAlbum(index, id, paths);
  }
};

}  // namespace knobify::library
