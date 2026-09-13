#include <unity.h>

#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "CoverArtCache.h"
#include "TagResult.h"

using knobify::library::CoverArtCache;
using knobify::library::CoverWriter;
using knobify::library::DirectoryReader;
using knobify::library::FileOpener;
using knobify::library::FolderEntry;
using knobify::library::JpegDecoder;
using knobify::library::RawFile;
using knobify::library::TagResult;

void setUp() {}
void tearDown() {}

namespace {

class FakeRawFile : public RawFile {
 public:
  explicit FakeRawFile(std::vector<uint8_t> data) : data_(std::move(data)) {}
  size_t size() const override { return data_.size(); }
  bool seek(size_t position) override {
    if (position > data_.size()) return false;
    pos_ = position;
    return true;
  }
  size_t read(uint8_t *buf, size_t n) override {
    size_t available = data_.size() - pos_;
    size_t toRead = n < available ? n : available;
    std::memcpy(buf, data_.data() + pos_, toRead);
    pos_ += toRead;
    return toRead;
  }

 private:
  std::vector<uint8_t> data_;
  size_t pos_ = 0;
};

class FakeDirectoryReader : public DirectoryReader {
 public:
  void put(const std::string &path, std::vector<FolderEntry> entries) {
    dirs_[path] = std::move(entries);
  }
  std::vector<FolderEntry> listChildren(const std::string &path) override {
    auto it = dirs_.find(path);
    if (it == dirs_.end()) return {};
    return it->second;
  }

 private:
  std::map<std::string, std::vector<FolderEntry>> dirs_;
};

class FakeFileOpener : public FileOpener {
 public:
  void put(const std::string &path, std::vector<uint8_t> bytes) {
    files_[path] = std::move(bytes);
  }
  std::unique_ptr<RawFile> open(const std::string &path) override {
    auto it = files_.find(path);
    if (it == files_.end()) return nullptr;
    return std::make_unique<FakeRawFile>(it->second);
  }

 private:
  std::map<std::string, std::vector<uint8_t>> files_;
};

// Records what it was asked to decode and returns a fixed, recognizable
// pixel pattern so tests can tell which source produced it.
class FakeJpegDecoder : public JpegDecoder {
 public:
  bool decodeSquare(const uint8_t *jpegBytes, size_t jpegLen,
                     uint16_t outSize, std::vector<uint16_t> *outPixels) override {
    lastBytes.assign(jpegBytes, jpegBytes + jpegLen);
    ++callCount;
    if (shouldFail) return false;
    outPixels->assign(static_cast<size_t>(outSize) * outSize, 0x1234);
    return true;
  }

  std::vector<uint8_t> lastBytes;
  int callCount = 0;
  bool shouldFail = false;
};

class FakeCoverWriter : public CoverWriter {
 public:
  void writeCover(const std::string &albumFolderPath, uint16_t size,
                   const std::vector<uint16_t> &pixels) override {
    ++callCount;
    lastFolderPath = albumFolderPath;
    lastSize = size;
    lastPixels = pixels;
  }

  int callCount = 0;
  std::string lastFolderPath;
  uint16_t lastSize = 0;
  std::vector<uint16_t> lastPixels;
};

}  // namespace

void test_prefers_embedded_jpeg_over_folder_cover() {
  const std::vector<uint8_t> embeddedJpeg = {0xFF, 0xD8, 0x01, 0x02};
  std::vector<uint8_t> file = {'x', 'x'};  // padding before the "image"
  file.insert(file.end(), embeddedJpeg.begin(), embeddedJpeg.end());
  FakeRawFile trackFile(file);

  TagResult tags;
  tags.picture.present = true;
  tags.picture.offset = 2;
  tags.picture.length = embeddedJpeg.size();

  FakeDirectoryReader dirReader;
  dirReader.put("/Music/Artist/Album",
                {FolderEntry{"cover.jpg", false}});
  FakeFileOpener opener;
  opener.put("/Music/Artist/Album/cover.jpg", {0xFF, 0xD8, 0x99});
  FakeJpegDecoder decoder;
  FakeCoverWriter writer;

  CoverArtCache::ensureCoverCached("/Music/Artist/Album", trackFile, tags,
                                    dirReader, opener, decoder, writer);

  TEST_ASSERT_EQUAL_INT(1, decoder.callCount);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(embeddedJpeg.data(), decoder.lastBytes.data(),
                                 embeddedJpeg.size());
  TEST_ASSERT_EQUAL_INT(1, writer.callCount);
  TEST_ASSERT_EQUAL_STRING("/Music/Artist/Album", writer.lastFolderPath.c_str());
  TEST_ASSERT_EQUAL_UINT16(CoverArtCache::kCoverSize, writer.lastSize);
}

void test_falls_back_to_folder_cover_jpg_when_no_embedded_picture() {
  FakeRawFile trackFile(std::vector<uint8_t>{});
  TagResult tags;  // no embedded picture

  FakeDirectoryReader dirReader;
  dirReader.put("/Music/Artist/Album",
                {FolderEntry{"readme.txt", false},
                 FolderEntry{"cover.jpg", false}});
  FakeFileOpener opener;
  const std::vector<uint8_t> coverBytes = {0xFF, 0xD8, 0xAA, 0xBB};
  opener.put("/Music/Artist/Album/cover.jpg", coverBytes);
  FakeJpegDecoder decoder;
  FakeCoverWriter writer;

  CoverArtCache::ensureCoverCached("/Music/Artist/Album", trackFile, tags,
                                    dirReader, opener, decoder, writer);

  TEST_ASSERT_EQUAL_INT(1, decoder.callCount);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(coverBytes.data(), decoder.lastBytes.data(),
                                 coverBytes.size());
  TEST_ASSERT_EQUAL_INT(1, writer.callCount);
}

void test_folder_cover_lookup_is_case_insensitive_and_accepts_jpeg_extension() {
  FakeRawFile trackFile(std::vector<uint8_t>{});
  TagResult tags;

  FakeDirectoryReader dirReader;
  dirReader.put("/Music/Artist/Album",
                {FolderEntry{"Cover.JPEG", false}});
  FakeFileOpener opener;
  opener.put("/Music/Artist/Album/Cover.JPEG", {0xFF, 0xD8});
  FakeJpegDecoder decoder;
  FakeCoverWriter writer;

  CoverArtCache::ensureCoverCached("/Music/Artist/Album", trackFile, tags,
                                    dirReader, opener, decoder, writer);

  TEST_ASSERT_EQUAL_INT(1, writer.callCount);
}

void test_no_cover_found_does_nothing() {
  FakeRawFile trackFile(std::vector<uint8_t>{});
  TagResult tags;  // no embedded picture

  FakeDirectoryReader dirReader;
  dirReader.put("/Music/Artist/Album", {FolderEntry{"readme.txt", false}});
  FakeFileOpener opener;
  FakeJpegDecoder decoder;
  FakeCoverWriter writer;

  CoverArtCache::ensureCoverCached("/Music/Artist/Album", trackFile, tags,
                                    dirReader, opener, decoder, writer);

  TEST_ASSERT_EQUAL_INT(0, decoder.callCount);
  TEST_ASSERT_EQUAL_INT(0, writer.callCount);
}

void test_failed_decode_does_not_write_a_cover() {
  FakeRawFile trackFile(std::vector<uint8_t>{});
  TagResult tags;

  FakeDirectoryReader dirReader;
  dirReader.put("/Music/Artist/Album", {FolderEntry{"cover.jpg", false}});
  FakeFileOpener opener;
  opener.put("/Music/Artist/Album/cover.jpg", {0xFF, 0xD8});
  FakeJpegDecoder decoder;
  decoder.shouldFail = true;
  FakeCoverWriter writer;

  CoverArtCache::ensureCoverCached("/Music/Artist/Album", trackFile, tags,
                                    dirReader, opener, decoder, writer);

  TEST_ASSERT_EQUAL_INT(0, writer.callCount);
}

void test_album_folder_path_for_strips_filename() {
  std::string folder =
      CoverArtCache::albumFolderPathFor("/Music/Artist/Album/track.mp3");
  TEST_ASSERT_EQUAL_STRING("/Music/Artist/Album", folder.c_str());
}

void test_cache_filename_is_deterministic_for_same_path() {
  std::string a = CoverArtCache::cacheFileNameFor("/Music/Artist/Album");
  std::string b = CoverArtCache::cacheFileNameFor("/Music/Artist/Album");
  TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
}

void test_cache_filename_differs_for_different_paths() {
  std::string a = CoverArtCache::cacheFileNameFor("/Music/Artist/Album One");
  std::string b = CoverArtCache::cacheFileNameFor("/Music/Artist/Album Two");
  TEST_ASSERT_TRUE(a != b);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_prefers_embedded_jpeg_over_folder_cover);
  RUN_TEST(test_falls_back_to_folder_cover_jpg_when_no_embedded_picture);
  RUN_TEST(test_folder_cover_lookup_is_case_insensitive_and_accepts_jpeg_extension);
  RUN_TEST(test_no_cover_found_does_nothing);
  RUN_TEST(test_failed_decode_does_not_write_a_cover);
  RUN_TEST(test_album_folder_path_for_strips_filename);
  RUN_TEST(test_cache_filename_is_deterministic_for_same_path);
  RUN_TEST(test_cache_filename_differs_for_different_paths);
  return UNITY_END();
}
