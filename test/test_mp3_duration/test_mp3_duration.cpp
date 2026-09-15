#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "../test_library_tags/FakeRawFile.h"
#include "Mp3Duration.h"

using knobify::library::Mp3Duration;

void setUp() {}
void tearDown() {}

namespace {

// ID3v2 tag of `bodySize` bytes (syncsafe size), like a tag carrying a cover.
std::vector<uint8_t> id3Tag(uint32_t bodySize) {
  std::vector<uint8_t> tag = {'I', 'D', '3', 3, 0, 0,
                              static_cast<uint8_t>((bodySize >> 21) & 0x7F),
                              static_cast<uint8_t>((bodySize >> 14) & 0x7F),
                              static_cast<uint8_t>((bodySize >> 7) & 0x7F),
                              static_cast<uint8_t>(bodySize & 0x7F)};
  tag.resize(10 + bodySize, 0);
  return tag;
}

void putBigEndian(std::vector<uint8_t> &bytes, size_t at, uint32_t value) {
  bytes[at] = static_cast<uint8_t>(value >> 24);
  bytes[at + 1] = static_cast<uint8_t>(value >> 16);
  bytes[at + 2] = static_cast<uint8_t>(value >> 8);
  bytes[at + 3] = static_cast<uint8_t>(value);
}

// A first MPEG audio frame (417 bytes, zero-filled) with a Xing/Info or
// VBRI header at `headerOffset` from the frame start.
std::vector<uint8_t> frame(uint8_t b1, uint8_t b2, uint8_t b3, const char *id,
                           size_t headerOffset, uint32_t frames) {
  std::vector<uint8_t> f(417, 0);
  f[0] = 0xFF;
  f[1] = b1;
  f[2] = b2;
  f[3] = b3;
  std::memcpy(&f[headerOffset], id, 4);
  if (std::strcmp(id, "VBRI") == 0) {
    putBigEndian(f, headerOffset + 14, frames);
  } else {
    putBigEndian(f, headerOffset + 4, 0x0F);  // Frames, bytes, TOC, quality.
    putBigEndian(f, headerOffset + 8, frames);
  }
  return f;
}

std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

}  // namespace

void test_xing_frames_give_exact_duration_for_mpeg1_stereo() {
  // MPEG1 Layer III, 44.1 kHz, joint stereo: side info 32 bytes, so the
  // Xing header sits 36 bytes into the frame. 10510 frames * 1152 samples
  // / 44100 Hz = 274.55 s -- "Love Song (Live)" on the device.
  FakeRawFile file(frame(0xFB, 0x90, 0x64, "Xing", 36, 10510));
  TEST_ASSERT_EQUAL_UINT32(275, Mp3Duration::readSeconds(file));
}

void test_skips_id3v2_tag_before_the_first_frame() {
  FakeRawFile file(
      concat(id3Tag(69406), frame(0xFB, 0x90, 0x64, "Xing", 36, 7570)));
  // 7570 * 1152 / 44100 = 197.75 s.
  TEST_ASSERT_EQUAL_UINT32(198, Mp3Duration::readSeconds(file));
}

void test_info_header_of_a_cbr_encode_counts_too() {
  FakeRawFile file(frame(0xFB, 0x90, 0x64, "Info", 36, 4134));
  // 4134 * 1152 / 44100 = 107.99 s.
  TEST_ASSERT_EQUAL_UINT32(108, Mp3Duration::readSeconds(file));
}

void test_mono_frame_has_shorter_side_info() {
  // Channel mode 11 (mono): side info 17 bytes -> header at 21.
  FakeRawFile file(frame(0xFB, 0x90, 0xC4, "Xing", 21, 1000));
  // 1000 * 1152 / 44100 = 26.12 s.
  TEST_ASSERT_EQUAL_UINT32(26, Mp3Duration::readSeconds(file));
}

void test_mpeg2_uses_576_samples_per_frame() {
  // MPEG2 Layer III (0xF3), 22.05 kHz (index 00), stereo: side info 17.
  FakeRawFile file(frame(0xF3, 0x90, 0x64, "Xing", 21, 2000));
  // 2000 * 576 / 22050 = 52.24 s.
  TEST_ASSERT_EQUAL_UINT32(52, Mp3Duration::readSeconds(file));
}

void test_vbri_header_is_read_too() {
  // Fraunhofer VBRI always sits 32 bytes after the 4-byte frame header.
  FakeRawFile file(frame(0xFB, 0x90, 0x64, "VBRI", 36, 10510));
  TEST_ASSERT_EQUAL_UINT32(275, Mp3Duration::readSeconds(file));
}

void test_unknown_without_a_vbr_header() {
  std::vector<uint8_t> plain(417, 0);
  plain[0] = 0xFF;
  plain[1] = 0xFB;
  plain[2] = 0x90;
  plain[3] = 0x64;
  FakeRawFile file(plain);
  TEST_ASSERT_EQUAL_UINT32(0, Mp3Duration::readSeconds(file));
}

void test_unknown_for_non_mpeg_data() {
  FakeRawFile file(std::vector<uint8_t>(64, 0x42));
  TEST_ASSERT_EQUAL_UINT32(0, Mp3Duration::readSeconds(file));
}

void test_unknown_when_xing_lacks_the_frames_field() {
  std::vector<uint8_t> f = frame(0xFB, 0x90, 0x64, "Xing", 36, 10510);
  putBigEndian(f, 40, 0x0E);  // Flags without "frames".
  FakeRawFile file(f);
  TEST_ASSERT_EQUAL_UINT32(0, Mp3Duration::readSeconds(file));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_xing_frames_give_exact_duration_for_mpeg1_stereo);
  RUN_TEST(test_skips_id3v2_tag_before_the_first_frame);
  RUN_TEST(test_info_header_of_a_cbr_encode_counts_too);
  RUN_TEST(test_mono_frame_has_shorter_side_info);
  RUN_TEST(test_mpeg2_uses_576_samples_per_frame);
  RUN_TEST(test_vbri_header_is_read_too);
  RUN_TEST(test_unknown_without_a_vbr_header);
  RUN_TEST(test_unknown_for_non_mpeg_data);
  RUN_TEST(test_unknown_when_xing_lacks_the_frames_field);
  return UNITY_END();
}
