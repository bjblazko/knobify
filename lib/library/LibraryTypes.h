#pragma once

#include <cstdint>
#include <string>

namespace knobify::library {

using ArtistId = uint32_t;
using AlbumId = uint32_t;
using TrackId = uint32_t;

struct Artist {
  ArtistId id;
  std::string name;
};

struct Album {
  AlbumId id;
  ArtistId artistId;
  std::string title;
  uint16_t year;  // 0 if unknown.
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
