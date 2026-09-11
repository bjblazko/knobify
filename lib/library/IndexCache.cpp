#include "IndexCache.h"

#include <cstring>

namespace knobify::library {

namespace {

constexpr char kMagic[4] = {'K', 'L', 'I', 'B'};
constexpr uint16_t kFormatVersion = 1;

void appendU16(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendString(std::vector<uint8_t> &out, const std::string &s) {
  appendU16(out, static_cast<uint16_t>(s.size()));
  out.insert(out.end(), s.begin(), s.end());
}

// Small cursor-based reader over a byte buffer; every read is bounds
// checked, and `ok` latches false permanently on the first failure so
// callers don't have to check after every field.
class ByteReader {
 public:
  explicit ByteReader(const std::vector<uint8_t> &buf) : buf_(buf) {}

  bool ok() const { return ok_; }

  uint16_t readU16() {
    if (!ensure(2)) return 0;
    uint16_t v = static_cast<uint16_t>(buf_[pos_] | (buf_[pos_ + 1] << 8));
    pos_ += 2;
    return v;
  }

  uint32_t readU32() {
    if (!ensure(4)) return 0;
    uint32_t v = static_cast<uint32_t>(buf_[pos_]) |
                 (static_cast<uint32_t>(buf_[pos_ + 1]) << 8) |
                 (static_cast<uint32_t>(buf_[pos_ + 2]) << 16) |
                 (static_cast<uint32_t>(buf_[pos_ + 3]) << 24);
    pos_ += 4;
    return v;
  }

  std::string readString() {
    uint16_t len = readU16();
    if (!ok_ || !ensure(len)) return "";
    std::string s(reinterpret_cast<const char *>(&buf_[pos_]), len);
    pos_ += len;
    return s;
  }

  bool readBytes(uint8_t *out, size_t n) {
    if (!ensure(n)) return false;
    std::memcpy(out, &buf_[pos_], n);
    pos_ += n;
    return true;
  }

 private:
  bool ensure(size_t n) {
    if (!ok_ || pos_ + n > buf_.size()) {
      ok_ = false;
      return false;
    }
    return true;
  }

  const std::vector<uint8_t> &buf_;
  size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace

LibrarySignature computeSignature(FileLister &lister) {
  LibrarySignature sig;
  lister.reset();
  FileEntry entry;
  while (lister.next(entry)) {
    ++sig.fileCount;
    sig.sizeMtimeXor ^= (entry.size ^ entry.mtime);
  }
  return sig;
}

std::vector<uint8_t> IndexCache::encode(const LibraryIndex &index,
                                         const LibrarySignature &signature) {
  std::vector<uint8_t> out;
  out.insert(out.end(), kMagic, kMagic + 4);
  appendU16(out, kFormatVersion);
  appendU32(out, signature.fileCount);
  appendU32(out, signature.sizeMtimeXor);

  appendU32(out, static_cast<uint32_t>(index.artists.size()));
  appendU32(out, static_cast<uint32_t>(index.albums.size()));
  appendU32(out, static_cast<uint32_t>(index.tracks.size()));

  for (const auto &artist : index.artists) {
    appendU32(out, artist.id);
    appendString(out, artist.name);
  }
  for (const auto &album : index.albums) {
    appendU32(out, album.id);
    appendU32(out, album.artistId);
    appendString(out, album.title);
  }
  for (const auto &track : index.tracks) {
    appendU32(out, track.id);
    appendU32(out, track.albumId);
    appendString(out, track.title);
    appendU16(out, track.trackNumber);
    appendString(out, track.filePath);
  }

  return out;
}

bool IndexCache::decode(const std::vector<uint8_t> &bytes, LibraryIndex &index,
                         LibrarySignature &signature) {
  if (bytes.size() < 4 || std::memcmp(bytes.data(), kMagic, 4) != 0) {
    return false;
  }

  ByteReader reader(bytes);
  uint8_t magicSkip[4];
  reader.readBytes(magicSkip, 4);  // Already validated above; just skip.

  uint16_t version = reader.readU16();
  if (version != kFormatVersion) {
    return false;
  }

  signature.fileCount = reader.readU32();
  signature.sizeMtimeXor = reader.readU32();

  uint32_t artistCount = reader.readU32();
  uint32_t albumCount = reader.readU32();
  uint32_t trackCount = reader.readU32();

  LibraryIndex result;
  result.artists.reserve(artistCount);
  for (uint32_t i = 0; i < artistCount && reader.ok(); ++i) {
    Artist a;
    a.id = reader.readU32();
    a.name = reader.readString();
    result.artists.push_back(a);
  }
  result.albums.reserve(albumCount);
  for (uint32_t i = 0; i < albumCount && reader.ok(); ++i) {
    Album a;
    a.id = reader.readU32();
    a.artistId = reader.readU32();
    a.title = reader.readString();
    result.albums.push_back(a);
  }
  result.tracks.reserve(trackCount);
  for (uint32_t i = 0; i < trackCount && reader.ok(); ++i) {
    Track t;
    t.id = reader.readU32();
    t.albumId = reader.readU32();
    t.title = reader.readString();
    t.trackNumber = reader.readU16();
    t.filePath = reader.readString();
    result.tracks.push_back(t);
  }

  if (!reader.ok()) {
    return false;
  }

  index = std::move(result);
  return true;
}

}  // namespace knobify::library
