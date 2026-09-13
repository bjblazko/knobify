#pragma once

#include <algorithm>
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

  // Sorted by year (ascending), unknown-year (0) albums last, title as a
  // stable tie-break -- so an artist's albums read chronologically.
  std::vector<AlbumId> albumsFor(ArtistId artistId) const {
    std::vector<AlbumId> result;
    for (const auto &album : albums) {
      if (album.artistId == artistId) result.push_back(album.id);
    }
    std::sort(result.begin(), result.end(), [this](AlbumId a, AlbumId b) {
      const Album &left = albums[a];
      const Album &right = albums[b];
      uint32_t leftYear = left.year == 0 ? UINT32_MAX : left.year;
      uint32_t rightYear = right.year == 0 ? UINT32_MAX : right.year;
      if (leftYear != rightYear) return leftYear < rightYear;
      return left.title < right.title;
    });
    return result;
  }

  // Sorted by track number (ascending), unknown-number (0) tracks last,
  // title as a stable tie-break -- so an album's tracks read in order.
  std::vector<TrackId> tracksFor(AlbumId albumId) const {
    std::vector<TrackId> result;
    for (const auto &track : tracks) {
      if (track.albumId == albumId) result.push_back(track.id);
    }
    std::sort(result.begin(), result.end(), [this](TrackId a, TrackId b) {
      const Track &left = tracks[a];
      const Track &right = tracks[b];
      uint32_t leftNum = left.trackNumber == 0 ? UINT32_MAX : left.trackNumber;
      uint32_t rightNum = right.trackNumber == 0 ? UINT32_MAX : right.trackNumber;
      if (leftNum != rightNum) return leftNum < rightNum;
      return left.title < right.title;
    });
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

// Notified as LibraryScanner::scan() processes each file, so a caller
// can show scan progress (there's no other feedback otherwise on a
// device with no LEDs -- a slow or SD-error-prone scan can otherwise
// look identical to a dead board). Purely an observation hook; default
// no-op implementation costs scan callers nothing to ignore.
class ScanProgressListener {
 public:
  virtual ~ScanProgressListener() = default;
  virtual void onFileScanned(size_t filesScannedSoFar) = 0;

  // Optional per-file diagnostic hook: what path was processed, whether
  // it could be opened at all, and what tags (if any) were read from it.
  // Default no-op so existing listeners (e.g. one only showing a
  // progress count) don't need to implement it. Exists specifically for
  // debugging "why did the scan only find N tracks" on real hardware --
  // see AGENTS.md.
  virtual void onFileResult(const std::string & /*path*/, bool /*opened*/,
                             const TagResult & /*tags*/) {}
};

// Walks every file from a FileLister, tag-parses each one via TagReader,
// and groups the results into an in-memory Artist/Album/Track index --
// see docs/adr/0004-navigation-library-and-index-architecture.md.
// Grouping key is (artist name, album title): the first file seen for a
// given pair creates the Artist/Album; later files with the same names
// join it.
class LibraryScanner {
 public:
  static LibraryIndex scan(FileLister &lister, FileOpener &opener,
                            ScanProgressListener *progress = nullptr);
};

}  // namespace knobify::library
