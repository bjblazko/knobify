#include <unity.h>

#include <vector>

#include "Utf8.h"

using namespace knobify::library::utf8;

void setUp() {}
void tearDown() {}

namespace {

std::vector<uint8_t> le(std::initializer_list<uint16_t> units) {
  std::vector<uint8_t> out;
  for (uint16_t u : units) {
    out.push_back(static_cast<uint8_t>(u & 0xFF));
    out.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
  }
  return out;
}

std::vector<uint8_t> be(std::initializer_list<uint16_t> units) {
  std::vector<uint8_t> out;
  for (uint16_t u : units) {
    out.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(u & 0xFF));
  }
  return out;
}

}  // namespace

void test_from_latin1_encodes_umlaut() {
  const uint8_t data[] = {0xF6};  // 'ö' in Latin-1
  std::string result = fromLatin1(data, 1);
  // U+00F6 in UTF-8 is 0xC3 0xB6.
  TEST_ASSERT_EQUAL(2, result.size());
  TEST_ASSERT_EQUAL_UINT8(0xC3, static_cast<uint8_t>(result[0]));
  TEST_ASSERT_EQUAL_UINT8(0xB6, static_cast<uint8_t>(result[1]));
}

void test_from_latin1_ascii_passthrough() {
  const uint8_t data[] = {'B', 'j'};
  std::string result = fromLatin1(data, 2);
  TEST_ASSERT_EQUAL_STRING("Bj", result.c_str());
}

void test_from_utf16le_with_bom() {
  // BOM (0xFFFE as bytes FF FE little-endian) + "Björk"
  auto bytes = le({0xFEFF, 'B', 'j', 0x00F6, 'r', 'k'});
  // fromUtf16 doesn't itself strip the BOM -- callers pass `start` past
  // it -- so test the raw unit decode starting after the BOM.
  std::string result = fromUtf16(bytes.data() + 2, bytes.size() - 2, true);
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", result.c_str());
}

void test_from_utf16be_with_bom() {
  auto bytes = be({0xFEFF, 'B', 'j', 0x00F6, 'r', 'k'});
  std::string result = fromUtf16(bytes.data() + 2, bytes.size() - 2, false);
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", result.c_str());
}

void test_from_utf16le_without_bom() {
  auto bytes = le({'B', 'j', 0x00F6, 'r', 'k'});
  std::string result = fromUtf16(bytes.data(), bytes.size(), true);
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", result.c_str());
}

void test_from_utf16be_without_bom() {
  auto bytes = be({'B', 'j', 0x00F6, 'r', 'k'});
  std::string result = fromUtf16(bytes.data(), bytes.size(), false);
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", result.c_str());
}

void test_from_utf16_surrogate_pair() {
  // U+1F600 (grinning face) as a surrogate pair: D83D DE00.
  auto bytes = be({0xD83D, 0xDE00});
  std::string result = fromUtf16(bytes.data(), bytes.size(), false);
  // UTF-8 for U+1F600 is F0 9F 98 80.
  TEST_ASSERT_EQUAL(4, result.size());
  TEST_ASSERT_EQUAL_UINT8(0xF0, static_cast<uint8_t>(result[0]));
  TEST_ASSERT_EQUAL_UINT8(0x9F, static_cast<uint8_t>(result[1]));
  TEST_ASSERT_EQUAL_UINT8(0x98, static_cast<uint8_t>(result[2]));
  TEST_ASSERT_EQUAL_UINT8(0x80, static_cast<uint8_t>(result[3]));
}

void test_from_utf16_unpaired_surrogate_skipped() {
  // A lone high surrogate followed by an ordinary letter.
  auto bytes = be({0xD83D, 'A'});
  std::string result = fromUtf16(bytes.data(), bytes.size(), false);
  TEST_ASSERT_EQUAL_STRING("A", result.c_str());
}

void test_from_utf16_stops_at_nul() {
  auto bytes = be({'A', 'B', 0x0000, 'C'});
  std::string result = fromUtf16(bytes.data(), bytes.size(), false);
  TEST_ASSERT_EQUAL_STRING("AB", result.c_str());
}

void test_is_valid_utf8_accepts_ascii_and_umlaut() {
  TEST_ASSERT_TRUE(isValidUtf8("Bj\xC3\xB6rk"));
}

void test_is_valid_utf8_rejects_lone_high_bit_byte() {
  std::string s = "Bj";
  s.push_back(static_cast<char>(0xF6));  // raw Latin-1 'ö', not UTF-8
  s += "rk";
  TEST_ASSERT_FALSE(isValidUtf8(s));
}

void test_is_valid_utf8_rejects_overlong_encoding() {
  // Overlong 2-byte encoding of NUL (should be 1 byte: 0x00).
  std::string s;
  s.push_back(static_cast<char>(0xC0));
  s.push_back(static_cast<char>(0x80));
  TEST_ASSERT_FALSE(isValidUtf8(s));
}

void test_is_valid_utf8_rejects_encoded_surrogate() {
  // U+D800 encoded directly as 3 bytes (ED A0 80) -- never valid UTF-8.
  std::string s;
  s.push_back(static_cast<char>(0xED));
  s.push_back(static_cast<char>(0xA0));
  s.push_back(static_cast<char>(0x80));
  TEST_ASSERT_FALSE(isValidUtf8(s));
}

void test_is_valid_utf8_rejects_beyond_max_codepoint() {
  // F4 90 80 80 encodes U+110000, just past the U+10FFFF ceiling.
  std::string s;
  s.push_back(static_cast<char>(0xF4));
  s.push_back(static_cast<char>(0x90));
  s.push_back(static_cast<char>(0x80));
  s.push_back(static_cast<char>(0x80));
  TEST_ASSERT_FALSE(isValidUtf8(s));
}

void test_repair_leaves_valid_utf8_untouched() {
  std::string s = "Bj\xC3\xB6rk";
  std::string result = repair(s);
  TEST_ASSERT_EQUAL_STRING(s.c_str(), result.c_str());
}

void test_repair_fixes_latin1_byte() {
  std::string s = "Bj";
  s.push_back(static_cast<char>(0xF6));
  s += "rk";
  std::string result = repair(s);
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", result.c_str());
}

void test_truncate_keeps_short_string() {
  std::string result = truncate("hello", 10);
  TEST_ASSERT_EQUAL_STRING("hello", result.c_str());
}

void test_truncate_drops_incomplete_trailing_sequence() {
  // "Bj\xC3\xB6rk" with a cut of 3 bytes lands mid the 2-byte 'ö'
  // sequence (B=1, j=1, C3=lead byte of 'ö') -- the whole sequence
  // should be dropped, leaving just "Bj".
  std::string s = "Bj\xC3\xB6rk";
  std::string result = truncate(s, 3);
  TEST_ASSERT_EQUAL_STRING("Bj", result.c_str());
}

void test_truncate_keeps_complete_sequence_at_boundary() {
  std::string s = "Bj\xC3\xB6rk";  // B j C3 B6 r k = 6 bytes
  std::string result = truncate(s, 4);  // exactly through 'ö'
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6", result.c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_from_latin1_encodes_umlaut);
  RUN_TEST(test_from_latin1_ascii_passthrough);
  RUN_TEST(test_from_utf16le_with_bom);
  RUN_TEST(test_from_utf16be_with_bom);
  RUN_TEST(test_from_utf16le_without_bom);
  RUN_TEST(test_from_utf16be_without_bom);
  RUN_TEST(test_from_utf16_surrogate_pair);
  RUN_TEST(test_from_utf16_unpaired_surrogate_skipped);
  RUN_TEST(test_from_utf16_stops_at_nul);
  RUN_TEST(test_is_valid_utf8_accepts_ascii_and_umlaut);
  RUN_TEST(test_is_valid_utf8_rejects_lone_high_bit_byte);
  RUN_TEST(test_is_valid_utf8_rejects_overlong_encoding);
  RUN_TEST(test_is_valid_utf8_rejects_encoded_surrogate);
  RUN_TEST(test_is_valid_utf8_rejects_beyond_max_codepoint);
  RUN_TEST(test_repair_leaves_valid_utf8_untouched);
  RUN_TEST(test_repair_fixes_latin1_byte);
  RUN_TEST(test_truncate_keeps_short_string);
  RUN_TEST(test_truncate_drops_incomplete_trailing_sequence);
  RUN_TEST(test_truncate_keeps_complete_sequence_at_boundary);
  return UNITY_END();
}
