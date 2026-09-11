#include "VorbisCommentParser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace knobify::library {

namespace {

constexpr size_t kScanLimit = 64 * 1024;
const char kMagic[] = "\x03vorbis";
constexpr size_t kMagicLen = 7;

uint32_t readU32LE(const uint8_t *b) {
  return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
         (static_cast<uint32_t>(b[2]) << 16) |
         (static_cast<uint32_t>(b[3]) << 24);
}

std::string toUpper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                  [](unsigned char c) { return std::toupper(c); });
  return s;
}

}  // namespace

TagResult VorbisCommentParser::parse(RawFile &file) {
  TagResult result;

  size_t scanSize = std::min(file.size(), kScanLimit);
  if (scanSize < kMagicLen) {
    return result;
  }
  std::vector<uint8_t> buf(scanSize);
  if (!file.seek(0) || file.read(buf.data(), buf.size()) != buf.size()) {
    return result;
  }

  auto it = std::search(buf.begin(), buf.end(), kMagic, kMagic + kMagicLen);
  if (it == buf.end()) {
    return result;
  }
  size_t pos = static_cast<size_t>(it - buf.begin()) + kMagicLen;

  auto readU32 = [&](uint32_t &out) -> bool {
    if (pos + 4 > buf.size()) return false;
    out = readU32LE(&buf[pos]);
    pos += 4;
    return true;
  };

  uint32_t vendorLength = 0;
  if (!readU32(vendorLength) || pos + vendorLength > buf.size()) {
    return result;
  }
  pos += vendorLength;

  uint32_t commentCount = 0;
  if (!readU32(commentCount)) {
    return result;
  }

  bool any = false;
  for (uint32_t i = 0; i < commentCount; ++i) {
    uint32_t commentLength = 0;
    if (!readU32(commentLength) || pos + commentLength > buf.size()) {
      break;  // Truncated (likely past our scan window) -- stop cleanly.
    }
    std::string comment(reinterpret_cast<char *>(&buf[pos]), commentLength);
    pos += commentLength;

    auto eq = comment.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = toUpper(comment.substr(0, eq));
    std::string value = comment.substr(eq + 1);
    if (value.empty()) {
      continue;
    }

    if (key == "ARTIST") {
      result.artist = value;
      any = true;
    } else if (key == "ALBUM") {
      result.album = value;
      any = true;
    } else if (key == "TITLE") {
      result.title = value;
      any = true;
    } else if (key == "TRACKNUMBER") {
      uint16_t track = 0;
      for (char c : value) {
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        track = static_cast<uint16_t>(track * 10 + (c - '0'));
      }
      result.trackNumber = track;
      any = true;
    }
  }

  result.found = any;
  return result;
}

}  // namespace knobify::library
