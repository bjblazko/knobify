#include "Id3v2Parser.h"

#include <array>
#include <cctype>
#include <cstring>
#include <vector>

namespace knobify::library {

namespace {

uint32_t readSynchsafe(const uint8_t *b) {
  return (static_cast<uint32_t>(b[0] & 0x7F) << 21) |
         (static_cast<uint32_t>(b[1] & 0x7F) << 14) |
         (static_cast<uint32_t>(b[2] & 0x7F) << 7) |
         (static_cast<uint32_t>(b[3] & 0x7F));
}

uint32_t readPlain32(const uint8_t *b) {
  return (static_cast<uint32_t>(b[0]) << 24) |
         (static_cast<uint32_t>(b[1]) << 16) |
         (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

std::string trimTrailing(std::string s) {
  while (!s.empty() &&
         (s.back() == ' ' || s.back() == '\0' ||
          static_cast<unsigned char>(s.back()) < 0x20)) {
    s.pop_back();
  }
  return s;
}

// Text frame payload (after the leading encoding byte) -> a best-effort
// narrow string. Encodings 0 (Latin-1) and 3 (UTF-8) pass through
// verbatim; UTF-16 variants (1: BOM-prefixed, 2: big-endian, no BOM)
// keep only ASCII-range code units, dropping the rest -- adequate for
// the common case, not a full Unicode implementation.
std::string decodeText(uint8_t encoding, const uint8_t *data, size_t len) {
  std::string out;
  if (encoding == 0 || encoding == 3) {
    out.assign(reinterpret_cast<const char *>(data), len);
    // Stop at embedded null terminator, if any.
    auto nul = out.find('\0');
    if (nul != std::string::npos) {
      out.resize(nul);
    }
    return trimTrailing(out);
  }

  bool littleEndian = true;
  size_t start = 0;
  if (encoding == 1 && len >= 2) {
    if (data[0] == 0xFF && data[1] == 0xFE) {
      littleEndian = true;
      start = 2;
    } else if (data[0] == 0xFE && data[1] == 0xFF) {
      littleEndian = false;
      start = 2;
    }
  } else if (encoding == 2) {
    littleEndian = false;
  }

  for (size_t i = start; i + 1 < len; i += 2) {
    uint16_t unit = littleEndian ? (data[i] | (data[i + 1] << 8))
                                  : ((data[i] << 8) | data[i + 1]);
    if (unit == 0) {
      break;
    }
    if (unit < 0x80) {
      out.push_back(static_cast<char>(unit));
    }
  }
  return trimTrailing(out);
}

uint16_t parseLeadingNumber(const std::string &s) {
  uint16_t value = 0;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      break;
    }
    value = static_cast<uint16_t>(value * 10 + (c - '0'));
  }
  return value;
}

// Trailing 128-byte ID3v1 tag, used when there's no ID3v2 header.
TagResult parseId3v1(RawFile &file) {
  TagResult result;
  size_t fileSize = file.size();
  if (fileSize < 128) {
    return result;
  }
  std::array<uint8_t, 128> buf{};
  if (!file.seek(fileSize - 128)) {
    return result;
  }
  if (file.read(buf.data(), buf.size()) != buf.size()) {
    return result;
  }
  if (std::memcmp(buf.data(), "TAG", 3) != 0) {
    return result;
  }

  result.found = true;
  result.title =
      trimTrailing(std::string(reinterpret_cast<char *>(&buf[3]), 30));
  result.artist =
      trimTrailing(std::string(reinterpret_cast<char *>(&buf[33]), 30));
  result.album =
      trimTrailing(std::string(reinterpret_cast<char *>(&buf[63]), 30));
  // ID3v1.1: a zero byte at offset 125 (comment[28]) means byte 126 is a
  // track number, not part of the comment.
  if (buf[125] == 0 && buf[126] != 0) {
    result.trackNumber = buf[126];
  }
  return result;
}

}  // namespace

TagResult Id3v2Parser::parse(RawFile &file) {
  std::array<uint8_t, 10> header{};
  if (!file.seek(0) || file.read(header.data(), header.size()) != 10 ||
      std::memcmp(header.data(), "ID3", 3) != 0) {
    return parseId3v1(file);
  }

  uint8_t majorVersion = header[3];
  uint32_t tagSize = readSynchsafe(&header[6]);
  size_t frameAreaEnd = 10 + tagSize;
  size_t pos = 10;

  TagResult result;
  bool any = false;

  while (pos + 10 <= frameAreaEnd) {
    std::array<uint8_t, 10> frameHeader{};
    if (!file.seek(pos) ||
        file.read(frameHeader.data(), frameHeader.size()) != 10) {
      break;
    }
    // Padding: a null frame id means we've hit the end of real frames.
    if (frameHeader[0] == 0) {
      break;
    }
    std::string frameId(reinterpret_cast<char *>(frameHeader.data()), 4);
    uint32_t frameSize = (majorVersion >= 4)
                              ? readSynchsafe(&frameHeader[4])
                              : readPlain32(&frameHeader[4]);
    size_t dataStart = pos + 10;
    if (frameSize == 0 || dataStart + frameSize > frameAreaEnd + 10) {
      break;  // Malformed/truncated frame; stop rather than misread.
    }

    if (frameId == "TIT2" || frameId == "TPE1" || frameId == "TALB" ||
        frameId == "TRCK" || frameId == "TYER" || frameId == "TDRC") {
      std::vector<uint8_t> data(frameSize);
      if (!file.seek(dataStart) ||
          file.read(data.data(), data.size()) != data.size()) {
        break;
      }
      if (!data.empty()) {
        uint8_t encoding = data[0];
        std::string text = decodeText(encoding, data.data() + 1,
                                       data.size() - 1);
        if (!text.empty()) {
          any = true;
          if (frameId == "TIT2") {
            result.title = text;
          } else if (frameId == "TPE1") {
            result.artist = text;
          } else if (frameId == "TALB") {
            result.album = text;
          } else if (frameId == "TRCK") {
            result.trackNumber = parseLeadingNumber(text);
          } else if (frameId == "TYER" || frameId == "TDRC") {
            // TYER (v2.3) is just "YYYY"; TDRC (v2.4) is an ISO 8601
            // timestamp ("YYYY" or "YYYY-MM-DD..."), so take the leading
            // digits either way. Don't overwrite a TYER already read with
            // a later, possibly-absent TDRC value in the same file.
            uint16_t year = parseLeadingNumber(text);
            if (year != 0) {
              result.year = year;
            }
          }
        }
      }
    }

    pos = dataStart + frameSize;
  }

  if (any) {
    result.found = true;
    return result;
  }
  // ID3v2 header present but no usable text frames -- still worth trying
  // the v1 trailer in case only that was written.
  return parseId3v1(file);
}

}  // namespace knobify::library
