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

  // `order` must match what the list screen showed, so a shuffle row and
  // the rows under it agree on what "this artist, in order" means.
  static std::vector<std::string> forArtist(const LibraryIndex &index,
                                            SortOrder order,
                                            ArtistId artistId) {
    std::vector<std::string> paths;
    appendArtist(index, order, artistId, paths);
    return paths;
  }

  static std::vector<std::string> forLibrary(const LibraryIndex &index,
                                             SortOrder order) {
    std::vector<std::string> paths;
    paths.reserve(index.tracks.size());
    for (ArtistId id : index.artistsSorted(order)) {
      appendArtist(index, order, id, paths);
    }
    return paths;
  }

 private:
  static void appendAlbum(const LibraryIndex &index, AlbumId albumId,
                          std::vector<std::string> &paths) {
    for (TrackId id : index.tracksFor(albumId)) {
      paths.push_back(index.tracks[id].filePath);
    }
  }

  static void appendArtist(const LibraryIndex &index, SortOrder order,
                           ArtistId artistId,
                           std::vector<std::string> &paths) {
    for (AlbumId id : index.albumsFor(artistId, order)) {
      appendAlbum(index, id, paths);
    }
  }
};

}  // namespace knobify::library
