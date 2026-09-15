#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../test_library_tags/FakeRawFile.h"
#include "Mp4Parser.h"
#include "TagReader.h"

using knobify::library::Mp4Info;
using knobify::library::Mp4Parser;
using knobify::library::TagReader;
using knobify::library::TagResult;

void setUp() {}
void tearDown() {}

namespace {

using Bytes = std::vector<uint8_t>;

void appendU32(Bytes &out, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(static_cast<uint8_t>(v >> shift));
  }
}

Bytes atom(const std::string &type, const Bytes &payload) {
  Bytes out;
  appendU32(out, static_cast<uint32_t>(8 + payload.size()));
  out.insert(out.end(), type.begin(), type.end());
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

Bytes concat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes &p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

// An ilst item: `name` holding one `data` atom of `type`.
Bytes item(const std::string &name, uint32_t type, const Bytes &value) {
  Bytes data;
  appendU32(data, type);
  appendU32(data, 0);  // Locale.
  data.insert(data.end(), value.begin(), value.end());
  return atom(name, atom("data", data));
}

Bytes text(const std::string &s) { return Bytes(s.begin(), s.end()); }

Bytes indexNumber(uint16_t n, uint16_t total) {
  return {0, 0, static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n),
          static_cast<uint8_t>(total >> 8), static_cast<uint8_t>(total), 0, 0};
}

Bytes mvhdV0(uint32_t timescale, uint32_t duration) {
  Bytes p = {0, 0, 0, 0};
  appendU32(p, 0);  // Creation time.
  appendU32(p, 0);  // Modification time.
  appendU32(p, timescale);
  appendU32(p, duration);
  p.resize(100, 0);
  return atom("mvhd", p);
}

Bytes meta(const Bytes &ilstPayload) {
  Bytes p = {0, 0, 0, 0};  // Full box version/flags.
  Bytes ilst = atom("ilst", ilstPayload);
  p.insert(p.end(), ilst.begin(), ilst.end());
  return atom("meta", p);
}

const Bytes kJpeg = {0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3, 4};

Bytes fullIlst() {
  return concat({item("\xA9nam", 1, text("Sexy Boy")),
                 item("\xA9" "ART", 1, text("Air")),
                 item("aART", 1, text("Air & Friends")),
                 item("\xA9" "alb", 1, text("Moon Safari")),
                 item("trkn", 0, indexNumber(2, 10)),
                 item("disk", 0, indexNumber(1, 2)),
                 item("\xA9" "day", 1, text("1998-01-16T08:00:00Z")),
                 item("covr", 13, kJpeg)});
}

Bytes moov(const Bytes &ilstPayload) {
  return atom("moov", concat({mvhdV0(44100, 44100 * 298 + 22050),
                              atom("udta", meta(ilstPayload))}));
}

Bytes ftyp() { return atom("ftyp", text("M4A \0\0\0\0")); }

}  // namespace

void test_parses_tags_duration_and_mdat_with_moov_first() {
  Bytes mdat = atom("mdat", Bytes(1000, 0x55));
  Bytes file = concat({ftyp(), moov(fullIlst()), mdat});
  FakeRawFile raw(file);

  Mp4Info info = Mp4Parser::parse(raw);

  TEST_ASSERT_TRUE(info.tags.found);
  TEST_ASSERT_EQUAL_STRING("Sexy Boy", info.tags.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Air", info.tags.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Moon Safari", info.tags.album.c_str());
  TEST_ASSERT_EQUAL_UINT16(2, info.tags.trackNumber);
  TEST_ASSERT_EQUAL_UINT16(1, info.tags.discNumber);
  TEST_ASSERT_EQUAL_UINT16(1998, info.tags.year);
  TEST_ASSERT_EQUAL_UINT32(298500, info.durationMs);
  TEST_ASSERT_EQUAL(file.size() - 1000, info.mdatStart);
  TEST_ASSERT_EQUAL(file.size(), info.mdatEnd);

  TEST_ASSERT_TRUE(info.tags.picture.present);
  TEST_ASSERT_EQUAL(kJpeg.size(), info.tags.picture.length);
  TEST_ASSERT_EQUAL(0xFF, file[info.tags.picture.offset]);
  TEST_ASSERT_EQUAL(0xD8, file[info.tags.picture.offset + 1]);
}

void test_moov_after_mdat() {
  Bytes file = concat({ftyp(), atom("mdat", Bytes(500, 1)), moov(fullIlst())});
  FakeRawFile raw(file);

  Mp4Info info = Mp4Parser::parse(raw);

  TEST_ASSERT_EQUAL_STRING("Sexy Boy", info.tags.title.c_str());
  TEST_ASSERT_EQUAL(ftyp().size() + 8, info.mdatStart);
  TEST_ASSERT_EQUAL(ftyp().size() + 8 + 500, info.mdatEnd);
  TEST_ASSERT_TRUE(info.tags.picture.present);
}

void test_album_artist_is_fallback_for_missing_artist() {
  Bytes ilst = concat({item("aART", 1, text("Various")),
                       item("\xA9nam", 1, text("Song"))});
  FakeRawFile raw(concat({ftyp(), moov(ilst)}));

  TEST_ASSERT_EQUAL_STRING("Various", Mp4Parser::parse(raw).tags.artist.c_str());
}

void test_png_cover_is_ignored_and_untyped_jpeg_accepted() {
  Bytes png = {0x89, 'P', 'N', 'G'};
  FakeRawFile pngOnly(concat({ftyp(), moov(item("covr", 14, png))}));
  TEST_ASSERT_FALSE(Mp4Parser::parse(pngOnly).tags.picture.present);

  FakeRawFile untyped(concat({ftyp(), moov(item("covr", 0, kJpeg))}));
  TEST_ASSERT_TRUE(Mp4Parser::parse(untyped).tags.picture.present);
}

void test_mvhd_version_1() {
  Bytes p = {1, 0, 0, 0};
  for (int i = 0; i < 16; ++i) p.push_back(0);  // 64-bit times.
  appendU32(p, 1000);                           // Timescale.
  appendU32(p, 0);
  appendU32(p, 123456);  // 64-bit duration.
  p.resize(112, 0);
  FakeRawFile raw(concat({ftyp(), atom("moov", atom("mvhd", p))}));

  TEST_ASSERT_EQUAL_UINT32(123456, Mp4Parser::parse(raw).durationMs);
}

void test_truncated_and_garbage_files_do_not_crash() {
  Bytes file = concat({ftyp(), moov(fullIlst())});
  for (size_t cut = 0; cut < file.size(); cut += 7) {
    FakeRawFile raw(Bytes(file.begin(), file.begin() + cut));
    Mp4Parser::parse(raw);
  }
  // An atom claiming to be larger than the file.
  Bytes bogus = {0x7F, 0xFF, 0xFF, 0xFF, 'm', 'o', 'o', 'v', 0, 0};
  FakeRawFile raw(bogus);
  Mp4Info info = Mp4Parser::parse(raw);
  TEST_ASSERT_FALSE(info.tags.found);
  TEST_ASSERT_EQUAL_UINT32(0, info.durationMs);
}

void test_tag_reader_dispatches_m4a() {
  FakeRawFile raw(concat({ftyp(), moov(fullIlst()), atom("mdat", Bytes(4, 0))}));

  TagResult tags = TagReader::read(raw, "/Music/Air/1998 Moon Safari/02 X.M4A");

  TEST_ASSERT_EQUAL_STRING("Sexy Boy", tags.title.c_str());
  TEST_ASSERT_TRUE(tags.picture.present);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_tags_duration_and_mdat_with_moov_first);
  RUN_TEST(test_moov_after_mdat);
  RUN_TEST(test_album_artist_is_fallback_for_missing_artist);
  RUN_TEST(test_png_cover_is_ignored_and_untyped_jpeg_accepted);
  RUN_TEST(test_mvhd_version_1);
  RUN_TEST(test_truncated_and_garbage_files_do_not_crash);
  RUN_TEST(test_tag_reader_dispatches_m4a);
  return UNITY_END();
}
