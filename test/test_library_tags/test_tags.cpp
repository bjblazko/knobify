#include <unity.h>

#include <cstring>
#include <vector>

#include "FakeRawFile.h"
#include "Id3v2Parser.h"
#include "RiffInfoParser.h"
#include "TagReader.h"
#include "VorbisCommentParser.h"

using knobify::library::Id3v2Parser;
using knobify::library::RiffInfoParser;
using knobify::library::TagReader;
using knobify::library::TagResult;
using knobify::library::VorbisCommentParser;

void setUp() {}
void tearDown() {}

namespace {

void appendU32BE(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void appendSynchsafe(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 21) & 0x7F));
  out.push_back(static_cast<uint8_t>((v >> 14) & 0x7F));
  out.push_back(static_cast<uint8_t>((v >> 7) & 0x7F));
  out.push_back(static_cast<uint8_t>(v & 0x7F));
}

void appendFrameV3(std::vector<uint8_t> &frames, const char *id,
                    const std::string &text) {
  std::vector<uint8_t> data;
  data.push_back(0);  // encoding: ISO-8859-1
  data.insert(data.end(), text.begin(), text.end());

  frames.insert(frames.end(), id, id + 4);
  appendU32BE(frames, static_cast<uint32_t>(data.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), data.begin(), data.end());
}

std::vector<uint8_t> buildId3v2Mp3(const std::vector<
    std::pair<const char *, std::string>> &fields) {
  std::vector<uint8_t> frames;
  for (auto &f : fields) {
    appendFrameV3(frames, f.first, f.second);
  }

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  // A little "audio" payload after the tag, like a real file.
  for (int i = 0; i < 16; ++i) file.push_back(0xAB);
  return file;
}

std::vector<uint8_t> buildId3v1Trailer(const std::string &title,
                                        const std::string &artist,
                                        const std::string &album) {
  std::vector<uint8_t> file(200, 0);  // fake "audio" body
  std::vector<uint8_t> tag(128, 0);
  tag[0] = 'T';
  tag[1] = 'A';
  tag[2] = 'G';
  std::memcpy(&tag[3], title.data(), title.size());
  std::memcpy(&tag[33], artist.data(), artist.size());
  std::memcpy(&tag[63], album.data(), album.size());
  file.insert(file.end(), tag.begin(), tag.end());
  return file;
}

}  // namespace

void test_id3v2_parses_title_artist_album_track() {
  auto file = buildId3v2Mp3({{"TIT2", "Song Title"},
                              {"TPE1", "The Artist"},
                              {"TALB", "The Album"},
                              {"TRCK", "3/12"}});
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("Song Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("The Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("The Album", result.album.c_str());
  TEST_ASSERT_EQUAL_UINT16(3, result.trackNumber);
}

void test_id3v2_missing_tag_returns_not_found() {
  std::vector<uint8_t> file(64, 0xAB);  // no "ID3" header, no v1 trailer
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_id3v2_falls_back_to_v1_trailer() {
  auto file = buildId3v1Trailer("V1 Title", "V1 Artist", "V1 Album");
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("V1 Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("V1 Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("V1 Album", result.album.c_str());
}

void test_id3v2_extracts_embedded_jpeg_picture() {
  std::vector<uint8_t> apicData;
  apicData.push_back(0);  // text encoding: Latin-1
  const char *mime = "image/jpeg";
  apicData.insert(apicData.end(), mime, mime + std::strlen(mime) + 1);
  apicData.push_back(0x03);  // picture type: front cover
  apicData.push_back(0);     // description: empty, null-terminated
  const std::vector<uint8_t> jpegBytes = {0xFF, 0xD8, 0xFF, 0xAA, 0xBB, 0xCC};
  apicData.insert(apicData.end(), jpegBytes.begin(), jpegBytes.end());

  std::vector<uint8_t> frames;
  frames.insert(frames.end(), {'A', 'P', 'I', 'C'});
  appendU32BE(frames, static_cast<uint32_t>(apicData.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), apicData.begin(), apicData.end());

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.picture.present);
  TEST_ASSERT_EQUAL_UINT32(jpegBytes.size(), result.picture.length);

  std::vector<uint8_t> extracted(result.picture.length);
  TEST_ASSERT_TRUE(raw.seek(result.picture.offset));
  TEST_ASSERT_EQUAL_UINT32(extracted.size(),
                            raw.read(extracted.data(), extracted.size()));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(jpegBytes.data(), extracted.data(),
                                 jpegBytes.size());
}

void test_id3v2_ignores_non_jpeg_picture() {
  std::vector<uint8_t> apicData;
  apicData.push_back(0);
  const char *mime = "image/png";
  apicData.insert(apicData.end(), mime, mime + std::strlen(mime) + 1);
  apicData.push_back(0x03);
  apicData.push_back(0);
  const std::vector<uint8_t> pngBytes = {0x89, 'P', 'N', 'G'};
  apicData.insert(apicData.end(), pngBytes.begin(), pngBytes.end());

  std::vector<uint8_t> frames;
  frames.insert(frames.end(), {'A', 'P', 'I', 'C'});
  appendU32BE(frames, static_cast<uint32_t>(apicData.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), apicData.begin(), apicData.end());

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_FALSE(result.picture.present);
}

void test_id3v2_apic_only_no_text_frames_still_found() {
  std::vector<uint8_t> apicData;
  apicData.push_back(0);
  const char *mime = "image/jpeg";
  apicData.insert(apicData.end(), mime, mime + std::strlen(mime) + 1);
  apicData.push_back(0x03);
  apicData.push_back(0);
  const std::vector<uint8_t> jpegBytes = {0xFF, 0xD8, 0xFF};
  apicData.insert(apicData.end(), jpegBytes.begin(), jpegBytes.end());

  std::vector<uint8_t> frames;
  frames.insert(frames.end(), {'A', 'P', 'I', 'C'});
  appendU32BE(frames, static_cast<uint32_t>(apicData.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), apicData.begin(), apicData.end());

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_TRUE(result.picture.present);
}

void test_id3v2_truncated_apic_frame_does_not_crash() {
  std::vector<uint8_t> frames;
  frames.insert(frames.end(), {'A', 'P', 'I', 'C'});
  appendU32BE(frames, 100000);  // bogus, way past the tag
  frames.push_back(0);
  frames.push_back(0);
  frames.push_back(0);  // encoding byte only, no real payload

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_FALSE(result.picture.present);
}

void test_id3v2_truncated_frame_does_not_crash_and_yields_no_field() {
  // A frame header claiming a size far larger than the tag actually has.
  std::vector<uint8_t> frames;
  const char id[4] = {'T', 'I', 'T', '2'};
  frames.insert(frames.end(), id, id + 4);
  appendU32BE(frames, 100000);  // bogus, way past the tag
  frames.push_back(0);
  frames.push_back(0);
  frames.push_back(0);  // encoding byte only, no real payload

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());

  FakeRawFile raw(file);
  TagResult result = Id3v2Parser::parse(raw);

  // Should not crash; no usable frame was read so it falls through to
  // (absent) ID3v1 -- not found.
  TEST_ASSERT_FALSE(result.found);
}

void test_vorbis_comment_parses_fields() {
  std::vector<uint8_t> buf;
  buf.push_back(0x03);
  const char *vorbis = "vorbis";
  buf.insert(buf.end(), vorbis, vorbis + 6);

  auto appendU32LE = [&](uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  auto appendComment = [&](const std::string &s) {
    appendU32LE(static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
  };

  std::string vendor = "knobify test encoder";
  appendU32LE(static_cast<uint32_t>(vendor.size()));
  buf.insert(buf.end(), vendor.begin(), vendor.end());

  appendU32LE(4);  // comment count
  appendComment("ARTIST=Vorbis Artist");
  appendComment("ALBUM=Vorbis Album");
  appendComment("TITLE=Vorbis Title");
  appendComment("TRACKNUMBER=7");

  FakeRawFile raw(buf);
  TagResult result = VorbisCommentParser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("Vorbis Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Vorbis Album", result.album.c_str());
  TEST_ASSERT_EQUAL_STRING("Vorbis Title", result.title.c_str());
  TEST_ASSERT_EQUAL_UINT16(7, result.trackNumber);
}

void test_vorbis_comment_missing_magic_returns_not_found() {
  std::vector<uint8_t> buf(128, 0x00);
  FakeRawFile raw(buf);

  TagResult result = VorbisCommentParser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_vorbis_comment_empty_list_returns_not_found() {
  std::vector<uint8_t> buf;
  buf.push_back(0x03);
  const char *vorbis = "vorbis";
  buf.insert(buf.end(), vorbis, vorbis + 6);
  auto appendU32LE = [&](uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  std::string vendor = "enc";
  appendU32LE(static_cast<uint32_t>(vendor.size()));
  buf.insert(buf.end(), vendor.begin(), vendor.end());
  appendU32LE(0);  // zero comments

  FakeRawFile raw(buf);
  TagResult result = VorbisCommentParser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_riff_info_parses_fields() {
  std::vector<uint8_t> info;
  const char infoTag[4] = {'I', 'N', 'F', 'O'};
  info.insert(info.end(), infoTag, infoTag + 4);

  auto appendSub = [&](const char *id, const std::string &value) {
    info.insert(info.end(), id, id + 4);
    uint32_t size = static_cast<uint32_t>(value.size());
    info.push_back(static_cast<uint8_t>(size & 0xFF));
    info.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
    info.push_back(static_cast<uint8_t>((size >> 16) & 0xFF));
    info.push_back(static_cast<uint8_t>((size >> 24) & 0xFF));
    info.insert(info.end(), value.begin(), value.end());
    if (size % 2 != 0) info.push_back(0);
  };
  appendSub("INAM", "WAV Title");
  appendSub("IART", "WAV Artist");
  appendSub("IPRD", "WAV Album");

  std::vector<uint8_t> file;
  file.insert(file.end(), {'R', 'I', 'F', 'F'});
  uint32_t riffSize = 4 + 8 + static_cast<uint32_t>(info.size());
  file.push_back(static_cast<uint8_t>(riffSize & 0xFF));
  file.push_back(static_cast<uint8_t>((riffSize >> 8) & 0xFF));
  file.push_back(static_cast<uint8_t>((riffSize >> 16) & 0xFF));
  file.push_back(static_cast<uint8_t>((riffSize >> 24) & 0xFF));
  file.insert(file.end(), {'W', 'A', 'V', 'E'});

  file.insert(file.end(), {'L', 'I', 'S', 'T'});
  uint32_t listSize = static_cast<uint32_t>(info.size());
  file.push_back(static_cast<uint8_t>(listSize & 0xFF));
  file.push_back(static_cast<uint8_t>((listSize >> 8) & 0xFF));
  file.push_back(static_cast<uint8_t>((listSize >> 16) & 0xFF));
  file.push_back(static_cast<uint8_t>((listSize >> 24) & 0xFF));
  file.insert(file.end(), info.begin(), info.end());

  FakeRawFile raw(file);
  TagResult result = RiffInfoParser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("WAV Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("WAV Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("WAV Album", result.album.c_str());
}

void test_riff_info_no_info_chunk_returns_not_found() {
  std::vector<uint8_t> file;
  file.insert(file.end(), {'R', 'I', 'F', 'F'});
  uint32_t riffSize = 4;
  file.push_back(static_cast<uint8_t>(riffSize & 0xFF));
  file.push_back(0);
  file.push_back(0);
  file.push_back(0);
  file.insert(file.end(), {'W', 'A', 'V', 'E'});

  FakeRawFile raw(file);
  TagResult result = RiffInfoParser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_tag_reader_falls_back_to_unknown_and_filename() {
  std::vector<uint8_t> file(32, 0x00);  // no valid tag of any kind
  FakeRawFile raw(file);

  TagResult result = TagReader::read(raw, "/Music/some track.mp3");

  TEST_ASSERT_EQUAL_STRING("Unknown Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Unknown Album", result.album.c_str());
  TEST_ASSERT_EQUAL_STRING("some track", result.title.c_str());
}

void test_tag_reader_uses_id3_when_present_for_mp3() {
  auto file = buildId3v2Mp3({{"TIT2", "Real Title"}, {"TPE1", "Real Artist"}});
  FakeRawFile raw(file);

  TagResult result = TagReader::read(raw, "/Music/whatever.mp3");

  TEST_ASSERT_EQUAL_STRING("Real Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Real Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Unknown Album", result.album.c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_id3v2_parses_title_artist_album_track);
  RUN_TEST(test_id3v2_missing_tag_returns_not_found);
  RUN_TEST(test_id3v2_falls_back_to_v1_trailer);
  RUN_TEST(test_id3v2_truncated_frame_does_not_crash_and_yields_no_field);
  RUN_TEST(test_id3v2_extracts_embedded_jpeg_picture);
  RUN_TEST(test_id3v2_ignores_non_jpeg_picture);
  RUN_TEST(test_id3v2_apic_only_no_text_frames_still_found);
  RUN_TEST(test_id3v2_truncated_apic_frame_does_not_crash);
  RUN_TEST(test_vorbis_comment_parses_fields);
  RUN_TEST(test_vorbis_comment_missing_magic_returns_not_found);
  RUN_TEST(test_vorbis_comment_empty_list_returns_not_found);
  RUN_TEST(test_riff_info_parses_fields);
  RUN_TEST(test_riff_info_no_info_chunk_returns_not_found);
  RUN_TEST(test_tag_reader_falls_back_to_unknown_and_filename);
  RUN_TEST(test_tag_reader_uses_id3_when_present_for_mp3);
  return UNITY_END();
}
