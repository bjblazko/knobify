#pragma once

#include <memory>
#include <vector>

#include "FileLister.h"
#include "LibraryTypes.h"
#include "RawFile.h"
#include "TagReader.h"

namespace knobify::library {

struct LibraryIndex {
  std::vector<Artist> artists;
  std::vector<Album> albums;
  std::vector<Track> tracks;

  std::vector<AlbumId> albumsFor(ArtistId artistId) const {
    std::vector<AlbumId> result;
    for (const auto &album : albums) {
      if (album.artistId == artistId) result.push_back(album.id);
    }
    return result;
  }

  std::vector<TrackId> tracksFor(AlbumId albumId) const {
    std::vector<TrackId> result;
    for (const auto &track : tracks) {
      if (track.albumId == albumId) result.push_back(track.id);
    }
    return result;
  }
};

// A raw file-reader factory abstraction so LibraryScanner can open each
// listed file to read its tags without depending on the SD API directly.
class FileOpener {
 public:
  virtual ~FileOpener() = default;
  // Returns nullptr if the file can't be opened.
  virtual std::unique_ptr<RawFile> open(const std::string &path) = 0;
};

// Walks every file from a FileLister, tag-parses each one via TagReader,
// and groups the results into an in-memory Artist/Album/Track index --
// see docs/adr/0004-navigation-library-and-index-architecture.md.
// Grouping key is (artist name, album title): the first file seen for a
// given pair creates the Artist/Album; later files with the same names
// join it.
class LibraryScanner {
 public:
  static LibraryIndex scan(FileLister &lister, FileOpener &opener);
};

}  // namespace knobify::library
