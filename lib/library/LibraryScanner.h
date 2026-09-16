#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <memory>
#include <vector>

#include "FileLister.h"
#include "LibraryTypes.h"
#include "RawFile.h"
#include "TagReader.h"

namespace knobify::library {

namespace detail {

// UTF-8 Latin-1 letters (0xC3 0x80..0xBF) folded to ASCII, which is what
// a German/European library needs to sort sensibly. Everything else is
// left alone -- this is a sort key, not a transliteration.
inline char foldLatin1(unsigned char second) {
  static constexpr char kFolded[] =
      "AAAAAAECEEEEIIIIDNOOOOO*OUUUUYPsaaaaaaeceeeeiiiidnooooo/ouuuuypy";
  if (second < 0x80 || second > 0xBF) return '\0';
  return kFolded[second - 0x80];
}

}  // namespace detail

struct LibraryIndex {
  std::vector<Artist> artists;
  std::vector<Album> albums;
  std::vector<Track> tracks;

  // Alphabetical, the way a reader scans a shelf: case is ignored, a
  // leading "The " doesn't count (The Beatles sits under B), and the
  // accented letters a German library is full of fold to their base
  // letter, so "Die Ärzte" sorts with A instead of after Z. Scan order
  // (which is SD directory order) is meaningless to a listener --
  // unsorted artists were unusable on a real library of ~100 of them.
  std::vector<ArtistId> artistsSorted() const {
    std::vector<ArtistId> result;
    result.reserve(artists.size());
    for (const auto &artist : artists) result.push_back(artist.id);
    std::sort(result.begin(), result.end(), [this](ArtistId a, ArtistId b) {
      const std::string left = artistSortKey(artists[a].name);
      const std::string right = artistSortKey(artists[b].name);
      if (left != right) return left < right;
      return artists[a].name < artists[b].name;  // Stable for equal keys.
    });
    return result;
  }

  // Exposed for testing the folding rules directly.
  static std::string artistSortKey(const std::string &name) {
    std::string key = foldAccents(name);
    for (char &c : key) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // Only as a prefix with something behind it: an artist actually
    // called "The" would otherwise sort under an empty key.
    if (key.size() > 4 && key.compare(0, 4, "the ") == 0) key.erase(0, 4);
    return key;
  }

  // "Björk" -> "Bjork", "Die Ärzte" -> "Die Arzte"; other bytes pass
  // through unchanged.
  static std::string foldAccents(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(name[i]);
      if (c == 0xC3 && i + 1 < name.size()) {
        const char folded =
            detail::foldLatin1(static_cast<unsigned char>(name[i + 1]));
        if (folded != '\0') {
          out.push_back(folded);
          ++i;
          continue;
        }
      }
      out.push_back(name[i]);
    }
    return out;
  }

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

  // Sorted by disc, then track number (ascending) -- unknown disc (0)
  // counts as disc 1, unknown-number (0) tracks go last within their disc,
  // title as a stable tie-break -- so an album's tracks read in order,
  // including multi-disc sets whose track numbers restart per disc.
  std::vector<TrackId> tracksFor(AlbumId albumId) const {
    std::vector<TrackId> result;
    for (const auto &track : tracks) {
      if (track.albumId == albumId) result.push_back(track.id);
    }
    std::sort(result.begin(), result.end(), [this](TrackId a, TrackId b) {
      const Track &left = tracks[a];
      const Track &right = tracks[b];
      uint32_t leftDisc = left.discNumber == 0 ? 1 : left.discNumber;
      uint32_t rightDisc = right.discNumber == 0 ? 1 : right.discNumber;
      if (leftDisc != rightDisc) return leftDisc < rightDisc;
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

  // Fired once per album, the first time a track belonging to it is
  // scanned -- the natural hook for one-time, per-album work like
  // extracting and caching cover art (see CoverArtCache), since it's
  // never fired again for later tracks in the same album. `file` is
  // still open and positioned arbitrarily (tag parsing already seeked
  // around in it); callers needing specific bytes must seek first.
  // Default no-op so existing listeners are unaffected.
  virtual void onNewAlbum(const std::string & /*albumFolderPath*/,
                           RawFile & /*file*/, const TagResult & /*tags*/) {}
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
