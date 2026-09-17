#pragma once

#include <cstdint>
#include <string>

namespace knobify::library::utf8 {

// Latin-1 (ISO-8859-1) is a strict subset of Unicode's first 256 code
// points, so each input byte maps 1:1 to a codepoint: 0x00-0x7F pass
// through as single-byte UTF-8, 0x80-0xFF need the two-byte UTF-8 form.
inline std::string fromLatin1(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    uint8_t b = data[i];
    if (b < 0x80) {
      out.push_back(static_cast<char>(b));
    } else {
      out.push_back(static_cast<char>(0xC0 | (b >> 6)));
      out.push_back(static_cast<char>(0x80 | (b & 0x3F)));
    }
  }
  return out;
}

// Appends codepoint `cp` to `out` as UTF-8. Caller guarantees
// `cp <= 0x10FFFF` and not a surrogate.
inline void appendUtf8(std::string &out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Decodes a run of UTF-16 code units (2 bytes each, in the byte order
// given by `littleEndian`) to UTF-8. Handles the full Basic Multilingual
// Plane plus surrogate pairs (for codepoints above U+FFFF, e.g. emoji);
// an unpaired high or low surrogate is silently skipped rather than
// emitted as-is, since a lone surrogate has no valid UTF-8 encoding.
// Stops at a U+0000 code unit (ID3v2 frames are null-terminated) or when
// input runs out.
inline std::string fromUtf16(const uint8_t *data, size_t len,
                              bool littleEndian) {
  std::string out;
  size_t i = 0;
  while (i + 1 < len) {
    uint16_t unit = littleEndian ? (data[i] | (data[i + 1] << 8))
                                  : ((data[i] << 8) | data[i + 1]);
    i += 2;
    if (unit == 0) {
      break;
    }
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      // High surrogate: needs a following low surrogate to form a pair.
      if (i + 1 < len) {
        uint16_t next = littleEndian ? (data[i] | (data[i + 1] << 8))
                                      : ((data[i] << 8) | data[i + 1]);
        if (next >= 0xDC00 && next <= 0xDFFF) {
          i += 2;
          uint32_t cp = 0x10000 + ((static_cast<uint32_t>(unit) - 0xD800)
                                    << 10) +
                        (static_cast<uint32_t>(next) - 0xDC00);
          appendUtf8(out, cp);
          continue;
        }
      }
      // Unpaired high surrogate: drop it.
      continue;
    }
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
      // Unpaired low surrogate: drop it.
      continue;
    }
    appendUtf8(out, unit);
  }
  return out;
}

// Real UTF-8 validation: correct continuation-byte counts and values,
// no overlong encodings, no encoded surrogates (U+D800-U+DFFF, which are
// only meaningful as UTF-16 halves), and nothing above U+10FFFF. Used to
// tell genuinely UTF-8 tag text apart from text that only looks
// plausible (e.g. a lone high-bit byte from Latin-1).
inline bool isValidUtf8(const std::string &s) {
  size_t i = 0;
  size_t n = s.size();
  while (i < n) {
    uint8_t b0 = static_cast<uint8_t>(s[i]);
    size_t extra;
    uint32_t cp;
    uint32_t minCp;
    if (b0 < 0x80) {
      ++i;
      continue;
    } else if ((b0 & 0xE0) == 0xC0) {
      extra = 1;
      cp = b0 & 0x1F;
      minCp = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
      extra = 2;
      cp = b0 & 0x0F;
      minCp = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
      extra = 3;
      cp = b0 & 0x07;
      minCp = 0x10000;
    } else {
      return false;  // Stray continuation byte or invalid lead byte.
    }
    if (i + extra >= n) {
      return false;  // Truncated sequence.
    }
    for (size_t k = 1; k <= extra; ++k) {
      uint8_t b = static_cast<uint8_t>(s[i + k]);
      if ((b & 0xC0) != 0x80) {
        return false;  // Not a continuation byte.
      }
      cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < minCp) {
      return false;  // Overlong encoding.
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) {
      return false;  // Encoded surrogate half -- never valid in UTF-8.
    }
    if (cp > 0x10FFFF) {
      return false;  // Beyond Unicode's range.
    }
    i += extra + 1;
  }
  return true;
}

// The "the tag lied about its encoding" fallback: if `s` is already
// valid UTF-8, it's returned untouched; otherwise its bytes are re-read
// as Latin-1 (the next most common tag encoding) and re-emitted as
// UTF-8, which is always well-formed input for LVGL.
inline std::string repair(std::string s) {
  if (isValidUtf8(s)) {
    return s;
  }
  return fromLatin1(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

// Trims `s` to at most `maxBytes` bytes without ever splitting a UTF-8
// sequence: if the cut point lands inside a multi-byte sequence, the
// whole (incomplete) sequence is dropped rather than truncated in half.
inline std::string truncate(std::string s, size_t maxBytes) {
  if (s.size() <= maxBytes) {
    return s;
  }
  if (maxBytes == 0) {
    return std::string();
  }
  // Find the lead byte of the sequence that byte (maxBytes - 1) belongs
  // to, by walking back over continuation bytes (10xxxxxx).
  size_t leadPos = maxBytes - 1;
  while (leadPos > 0 &&
         (static_cast<uint8_t>(s[leadPos]) & 0xC0) == 0x80) {
    --leadPos;
  }
  uint8_t lead = static_cast<uint8_t>(s[leadPos]);
  size_t seqLen = 1;
  if ((lead & 0xE0) == 0xC0) {
    seqLen = 2;
  } else if ((lead & 0xF0) == 0xE0) {
    seqLen = 3;
  } else if ((lead & 0xF8) == 0xF0) {
    seqLen = 4;
  }
  // If that sequence doesn't fully fit before maxBytes, drop it whole;
  // otherwise the cut at maxBytes already lands on a clean boundary.
  size_t end = (leadPos + seqLen > maxBytes) ? leadPos : maxBytes;
  s.resize(end);
  return s;
}

}  // namespace knobify::library::utf8
