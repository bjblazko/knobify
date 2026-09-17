#include "RiffInfoParser.h"

#include <array>
#include <cstring>
#include <vector>

#include "Id3Genres.h"

namespace knobify::library {

namespace {

uint32_t readU32LE(const uint8_t *b) {
  return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
         (static_cast<uint32_t>(b[2]) << 16) |
         (static_cast<uint32_t>(b[3]) << 24);
}

std::string trimNulls(std::string s) {
  while (!s.empty() && s.back() == '\0') {
    s.pop_back();
  }
  return s;
}

}  // namespace

TagResult RiffInfoParser::parse(RawFile &file) {
  TagResult result;

  std::array<uint8_t, 12> header{};
  if (!file.seek(0) || file.read(header.data(), header.size()) != 12 ||
      std::memcmp(header.data(), "RIFF", 4) != 0 ||
      std::memcmp(&header[8], "WAVE", 4) != 0) {
    return result;
  }

  size_t fileSize = file.size();
  size_t pos = 12;
  bool any = false;

  while (pos + 8 <= fileSize) {
    std::array<uint8_t, 8> chunkHeader{};
    if (!file.seek(pos) ||
        file.read(chunkHeader.data(), chunkHeader.size()) != 8) {
      break;
    }
    std::string chunkId(reinterpret_cast<char *>(chunkHeader.data()), 4);
    uint32_t chunkSize = readU32LE(&chunkHeader[4]);
    size_t dataStart = pos + 8;
    if (dataStart + chunkSize > fileSize) {
      break;  // Truncated/malformed; stop rather than misread.
    }

    if (chunkId == "LIST" && chunkSize >= 4) {
      std::vector<uint8_t> listData(chunkSize);
      if (!file.seek(dataStart) ||
          file.read(listData.data(), listData.size()) != listData.size()) {
        break;
      }
      if (std::memcmp(listData.data(), "INFO", 4) == 0) {
        size_t p = 4;
        while (p + 8 <= listData.size()) {
          std::string subId(reinterpret_cast<char *>(&listData[p]), 4);
          uint32_t subSize = readU32LE(&listData[p + 4]);
          size_t subDataStart = p + 8;
          if (subDataStart + subSize > listData.size()) {
            break;
          }
          std::string value(
              reinterpret_cast<char *>(&listData[subDataStart]), subSize);
          value = trimNulls(value);
          if (!value.empty()) {
            if (subId == "INAM") {
              result.title = value;
              any = true;
            } else if (subId == "IART") {
              result.artist = value;
              any = true;
            } else if (subId == "IPRD") {
              result.album = value;
              any = true;
            } else if (subId == "IGNR") {
              // RIFF INFO writes the genre as plain text, but taggers
              // converting from ID3 sometimes carry the numbered form
              // over, so resolve it the same way TCON is.
              result.genre = resolveId3GenreText(value);
              any = true;
            } else if (subId == "ICRD") {
              // Typically "YYYY" or "YYYY-MM-DD"; take the leading digits.
              uint16_t year = 0;
              for (char c : value) {
                if (!std::isdigit(static_cast<unsigned char>(c))) break;
                year = static_cast<uint16_t>(year * 10 + (c - '0'));
              }
              if (year != 0) {
                result.year = year;
                any = true;
              }
            } else if (subId == "ITRK") {
              uint16_t track = 0;
              for (char c : value) {
                if (!std::isdigit(static_cast<unsigned char>(c))) break;
                track = static_cast<uint16_t>(track * 10 + (c - '0'));
              }
              if (track != 0) {
                result.trackNumber = track;
                any = true;
              }
            }
          }
          // Sub-chunks are word-aligned (padded to even size).
          p = subDataStart + subSize + (subSize % 2);
        }
      }
    }

    // Top-level chunks are also word-aligned.
    pos = dataStart + chunkSize + (chunkSize % 2);
  }

  result.found = any;
  return result;
}

}  // namespace knobify::library
