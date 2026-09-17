#pragma once

#include <cstdint>
#include <string>

namespace knobify::library {

// How a browse list is ordered. Music is tagged well enough to sort
// artists by their tag (ignoring a leading "The", folding accents) and
// albums chronologically; spoken-word folders rarely carry usable tags,
// so their lists follow the plain name instead -- see
// docs/adr/0018-collections-and-menu-visibility.md.
enum class SortOrder : uint8_t { ByTag = 0, ByName = 1 };

using ArtistId = uint32_t;
using AlbumId = uint32_t;
using TrackId = uint32_t;
using GenreId = uint32_t;

struct Artist {
  ArtistId id;
  std::string name;
};

// A genre shelf. Id 0 is always the "unknown" entry with an empty name,
// so ids stay dense and an untagged album needs no separate flag.
struct Genre {
  GenreId id;
  std::string name;
};

struct Album {
  AlbumId id;
  ArtistId artistId;
  std::string title;
  uint16_t year;  // 0 if unknown.
  // Genre sits on the album, not the track: a shelf is browsed by album,
  // and one string per album instead of per track keeps the index cache
  // small. A compilation whose tracks disagree therefore shows the first
  // genre seen -- see docs/adr/0021.
  GenreId genreId = 0;
};

struct Track {
  TrackId id;
  AlbumId albumId;
  std::string title;
  uint16_t trackNumber;  // 0 if unknown.
  std::string filePath;
  uint16_t discNumber = 0;  // 0 if unknown (sorts as disc 1).
};

}  // namespace knobify::library
