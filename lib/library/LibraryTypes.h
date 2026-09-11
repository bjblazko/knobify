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
};

struct Track {
  TrackId id;
  AlbumId albumId;
  std::string title;
  uint16_t trackNumber;  // 0 if unknown.
  std::string filePath;
};

}  // namespace knobify::library
