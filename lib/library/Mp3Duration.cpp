#include "Mp3Duration.h"

#include <array>
#include <cstring>

namespace knobify::library {

namespace {

// How far past the ID3 tag to look for the first frame sync.
constexpr size_t kMaxSyncSearch = 4096;
// A first frame's header plus side info plus a VBRI header fits in this.
constexpr size_t kFrameWindow = 64;

uint32_t bigEndian(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// Where the audio starts: after an ID3v2 tag (10-byte header, syncsafe
// size, plus a 10-byte footer when flagged), or 0 without one.
size_t audioStart(RawFile &file) {
  std::array<uint8_t, 10> header{};
  if (!file.seek(0) || file.read(header.data(), header.size()) != header.size()) {
    return 0;
  }
  if (std::memcmp(header.data(), "ID3", 3) != 0) return 0;
  size_t size = (static_cast<size_t>(header[6] & 0x7F) << 21) |
                (static_cast<size_t>(header[7] & 0x7F) << 14) |
                (static_cast<size_t>(header[8] & 0x7F) << 7) | (header[9] & 0x7F);
  bool footer = (header[5] & 0x10) != 0;
  return 10 + size + (footer ? 10 : 0);
}

struct FrameInfo {
  bool mpeg1;
  uint32_t sampleRate;
  bool mono;
};

// A Layer III frame header, or false for anything else.
bool parseHeader(const uint8_t *h, FrameInfo &info) {
  if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
  uint8_t version = (h[1] >> 3) & 0x03;  // 00 MPEG2.5, 10 MPEG2, 11 MPEG1.
  uint8_t layer = (h[1] >> 1) & 0x03;    // 01 Layer III.
  uint8_t rateIndex = (h[2] >> 2) & 0x03;
  if (version == 0x01 || layer != 0x01 || rateIndex == 0x03) return false;
  static constexpr uint32_t kMpeg1Rates[3] = {44100, 48000, 32000};
  uint32_t rate = kMpeg1Rates[rateIndex];
  if (version == 0x02) rate /= 2;
  if (version == 0x00) rate /= 4;
  info.mpeg1 = version == 0x03;
  info.sampleRate = rate;
  info.mono = ((h[3] >> 6) & 0x03) == 0x03;
  return true;
}

}  // namespace

uint32_t Mp3Duration::readSeconds(RawFile &file) {
  size_t start = audioStart(file);
  std::array<uint8_t, kMaxSyncSearch + kFrameWindow> buf{};
  if (!file.seek(start)) return 0;
  size_t got = file.read(buf.data(), buf.size());
  if (got < kFrameWindow) return 0;

  for (size_t at = 0; at + kFrameWindow <= got; ++at) {
    FrameInfo info{};
    if (!parseHeader(&buf[at], info)) continue;

    const uint8_t *frame = &buf[at];
    uint32_t samplesPerFrame = info.mpeg1 ? 1152 : 576;
    size_t sideInfo = info.mpeg1 ? (info.mono ? 17 : 32) : (info.mono ? 9 : 17);
    uint32_t frames = 0;

    const uint8_t *xing = frame + 4 + sideInfo;
    const uint8_t *vbri = frame + 4 + 32;
    if (std::memcmp(xing, "Xing", 4) == 0 || std::memcmp(xing, "Info", 4) == 0) {
      uint32_t flags = bigEndian(xing + 4);
      if ((flags & 0x01) == 0) return 0;  // No frame count.
      frames = bigEndian(xing + 8);
    } else if (std::memcmp(vbri, "VBRI", 4) == 0) {
      frames = bigEndian(vbri + 14);
    } else {
      return 0;  // First frame found, but no VBR header: leave it to the decoder.
    }
    if (frames == 0) return 0;
    uint64_t samples = static_cast<uint64_t>(frames) * samplesPerFrame;
    return static_cast<uint32_t>((samples + info.sampleRate / 2) / info.sampleRate);
  }
  return 0;
}

}  // namespace knobify::library
