#pragma once

#include <cstdint>
#include <vector>

#include "ResumeRecord.h"

namespace knobify::resume {

// Binary form of a ResumeRecord (ADR 0012):
//
//   header:  "KRES" | u8 version | u16 payload length | u32 CRC-32(payload)
//   payload: sections of  u8 tag | u16 length | body
//
// All integers little-endian, strings are u16 length + bytes. decode()
// rejects anything it can't fully trust -- wrong magic/version, length or
// CRC mismatch, a section running past the payload, out-of-range values --
// and the caller then starts on Home. Unknown section tags are skipped, and
// bytes after the fields a section body is known to have are ignored, so new
// sections and fields can be appended without a version bump.
class ResumeCodec {
 public:
  static constexpr uint8_t kVersion = 1;
  // Well inside one NVS page; a record this big would be a bug anyway.
  static constexpr std::size_t kMaxEncodedSize = 3072;
  static constexpr std::size_t kMaxStringLength = 512;

  // Section tags -- stored values, never reuse one.
  static constexpr uint8_t kTagNavigation = 1;
  static constexpr uint8_t kTagMusic = 2;

  // Empty if the record doesn't fit kMaxEncodedSize or a string is too long.
  static std::vector<uint8_t> encode(const ResumeRecord &record);

  // On false `out` is left empty (no sections).
  static bool decode(const std::vector<uint8_t> &bytes, ResumeRecord &out);

  static uint32_t crc32(const uint8_t *data, std::size_t length);
};

}  // namespace knobify::resume
