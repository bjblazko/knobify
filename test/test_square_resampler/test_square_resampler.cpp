#include <unity.h>

#include <cstdint>
#include <vector>

#include "SquareResampler.h"

using knobify::library::SquareResampler;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kBlue = 0x001F;
constexpr uint16_t kWhite = 0xFFFF;

}  // namespace

void test_uniform_image_stays_uniform_both_directions() {
  std::vector<uint16_t> src(600 * 600, kWhite);
  std::vector<uint16_t> out;
  SquareResampler::resample(src.data(), 600, 600, 96, &out);
  TEST_ASSERT_EQUAL(96u * 96u, out.size());
  for (uint16_t p : out) TEST_ASSERT_EQUAL_HEX16(kWhite, p);

  std::vector<uint16_t> small(75 * 75, kRed);
  SquareResampler::resample(small.data(), 75, 75, 96, &out);
  for (uint16_t p : out) TEST_ASSERT_EQUAL_HEX16(kRed, p);
}

void test_crops_to_centered_square() {
  // 4 wide, 2 high: left and right columns blue, middle two red.
  std::vector<uint16_t> src = {kBlue, kRed, kRed, kBlue,
                               kBlue, kRed, kRed, kBlue};
  std::vector<uint16_t> out;
  SquareResampler::resample(src.data(), 4, 2, 2, &out);
  for (uint16_t p : out) TEST_ASSERT_EQUAL_HEX16(kRed, p);
}

void test_downscale_averages_colors() {
  // 2x2 checkerboard of white and black -> one mid-grey pixel.
  std::vector<uint16_t> src = {kWhite, 0, 0, kWhite};
  std::vector<uint16_t> out;
  SquareResampler::resample(src.data(), 2, 2, 1, &out);
  uint16_t p = out[0];
  TEST_ASSERT_UINT16_WITHIN(1, 16, p >> 11);
  TEST_ASSERT_UINT16_WITHIN(1, 32, (p >> 5) & 0x3F);
  TEST_ASSERT_UINT16_WITHIN(1, 16, p & 0x1F);
}

void test_upscale_keeps_edges_and_blends_between() {
  // Left half red, right half blue, doubled in size.
  std::vector<uint16_t> src = {kRed, kBlue, kRed, kBlue};
  std::vector<uint16_t> out;
  SquareResampler::resample(src.data(), 2, 2, 8, &out);
  TEST_ASSERT_EQUAL_HEX16(kRed, out[0]);
  TEST_ASSERT_EQUAL_HEX16(kBlue, out[7]);
  uint16_t middle = out[3];
  TEST_ASSERT_TRUE((middle >> 11) > 0 && (middle & 0x1F) > 0);
}

void test_degenerate_sizes_do_not_crash() {
  std::vector<uint16_t> out;
  SquareResampler::resample(nullptr, 0, 0, 96, &out);
  TEST_ASSERT_EQUAL(96u * 96u, out.size());
  uint16_t one = kRed;
  SquareResampler::resample(&one, 1, 1, 96, &out);
  TEST_ASSERT_EQUAL_HEX16(kRed, out[96 * 96 - 1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_uniform_image_stays_uniform_both_directions);
  RUN_TEST(test_crops_to_centered_square);
  RUN_TEST(test_downscale_averages_colors);
  RUN_TEST(test_upscale_keeps_edges_and_blends_between);
  RUN_TEST(test_degenerate_sizes_do_not_crash);
  return UNITY_END();
}
