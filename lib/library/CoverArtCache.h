#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "FolderBrowser.h"
#include "LibraryScanner.h"
#include "RawFile.h"
#include "TagResult.h"

namespace knobify::library {

// Decodes a JPEG byte buffer into a fixed-size square RGB565 pixel
// buffer (center-cropped/scaled as needed). Wraps TJpg_Decoder on
// device; fakeable on host for testing since TJpg_Decoder itself is an
// Arduino-ecosystem library that doesn't build for the `native` test
// environment.
class JpegDecoder {
 public:
  virtual ~JpegDecoder() = default;
  // Writes exactly outSize*outSize pixels into *outPixels on success.
  virtual bool decodeSquare(const uint8_t *jpegBytes, size_t jpegLen,
                             uint16_t outSize,
                             std::vector<uint16_t> *outPixels) = 0;
};

// Persists a decoded cover, keyed by the album's folder path. Wraps SD
// access on device; fakeable on host for testing.
class CoverWriter {
 public:
  virtual ~CoverWriter() = default;
  virtual void writeCover(const std::string &albumFolderPath, uint16_t size,
                           const std::vector<uint16_t> &pixels) = 0;
};

// Reads back a cached cover for an album folder, if one exists. Wraps SD
// access on device; fakeable on host for testing.
class CoverArtReader {
 public:
  virtual ~CoverArtReader() = default;
  // Returns true and fills outSize/outPixels (outSize*outSize entries)
  // if a cached cover exists for this album folder; false otherwise (no
  // cover was ever cached for it).
  virtual bool loadCover(const std::string &albumFolderPath,
                          uint16_t *outSize,
                          std::vector<uint16_t> *outPixels) = 0;
};

// Finds and caches an album's cover art the first time that album is
// scanned: an embedded ID3 JPEG (from the first track's tags) takes
// priority over a cover.jpg/cover.jpeg file in the album folder. Does
// nothing if neither source is present -- the Now Playing screen simply
// shows no cover in that case, matching its existing minimalism.
class CoverArtCache {
 public:
  // Fixed square size cached covers are decoded to -- matches the
  // Now Playing screen's round-safe display slot (ScreenManager.cpp).
  static constexpr uint16_t kCoverSize = 96;

  static void ensureCoverCached(const std::string &albumFolderPath,
                                 RawFile &firstTrackFile,
                                 const TagResult &firstTrackTags,
                                 DirectoryReader &dirReader,
                                 FileOpener &opener, JpegDecoder &decoder,
                                 CoverWriter &writer);

  // Deterministic filename (no directory, no extension) a cached cover
  // for this album folder is stored/looked-up under -- shared by the
  // scan-time writer and the Now Playing screen's render-time reader so
  // both agree on where a given album's cover lives, independent of the
  // library index (see CoverArtCache.h's class comment).
  static std::string cacheFileNameFor(const std::string &albumFolderPath);

  // Directory directly containing `trackFilePath` (i.e. the album
  // folder, for a file laid out as .../Artist/Album/track.mp3) -- what
  // the Now Playing screen derives from the currently-playing track's
  // path to look up its cached cover.
  static std::string albumFolderPathFor(const std::string &trackFilePath);
};

}  // namespace knobify::library
