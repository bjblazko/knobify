#include "ResumeCodec.h"

#include "CollectionId.h"

#include <string>

namespace knobify::resume {

namespace {

constexpr uint8_t kMagic[4] = {'K', 'R', 'E', 'S'};
constexpr std::size_t kHeaderSize = 4 + 1 + 2 + 4;

class Writer {
 public:
  void u8(uint8_t value) { bytes.push_back(value); }
  void u16(uint16_t value) {
    u8(static_cast<uint8_t>(value));
    u8(static_cast<uint8_t>(value >> 8));
  }
  void u32(uint32_t value) {
    u16(static_cast<uint16_t>(value));
    u16(static_cast<uint16_t>(value >> 16));
  }
  void str(const std::string &value) {
    if (value.size() > ResumeCodec::kMaxStringLength) ok = false;
    u16(static_cast<uint16_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
  }

  std::vector<uint8_t> bytes;
  bool ok = true;
};

// Every read checks bounds; after the first failure all reads fail.
class Reader {
 public:
  Reader(const uint8_t *data, std::size_t size) : data_(data), size_(size) {}

  bool u8(uint8_t &out) {
    if (!has(1)) return false;
    out = data_[pos_++];
    return true;
  }
  bool u16(uint16_t &out) {
    uint8_t low, high;
    if (!u8(low) || !u8(high)) return false;
    out = static_cast<uint16_t>(low | (high << 8));
    return true;
  }
  bool u32(uint32_t &out) {
    uint16_t low, high;
    if (!u16(low) || !u16(high)) return false;
    out = low | (static_cast<uint32_t>(high) << 16);
    return true;
  }
  bool str(std::string &out) {
    uint16_t length;
    if (!u16(length) || length > ResumeCodec::kMaxStringLength || !has(length)) {
      return fail();
    }
    out.assign(reinterpret_cast<const char *>(data_ + pos_), length);
    pos_ += length;
    return true;
  }
  // A sub-reader over the next `length` bytes, which this reader skips.
  bool sub(uint16_t length, Reader &out) {
    if (!has(length)) return false;
    out = Reader(data_ + pos_, length);
    pos_ += length;
    return true;
  }
  bool atEnd() const { return pos_ == size_; }

 private:
  bool has(std::size_t count) {
    if (failed_ || size_ - pos_ < count) return fail();
    return true;
  }
  bool fail() {
    failed_ = true;
    return false;
  }

  const uint8_t *data_;
  std::size_t size_;
  std::size_t pos_ = 0;
  bool failed_ = false;
};

void writeSection(Writer &payload, uint8_t tag, const Writer &body) {
  if (!body.ok || body.bytes.size() > UINT16_MAX) {
    payload.ok = false;
    return;
  }
  payload.u8(tag);
  payload.u16(static_cast<uint16_t>(body.bytes.size()));
  payload.bytes.insert(payload.bytes.end(), body.bytes.begin(), body.bytes.end());
}

bool readNavigation(Reader &in, NavigationSnapshot &out) {
  if (!in.u8(out.activeTab) || !in.u8(out.lastBrowseTab) ||
      !in.u8(out.collection)) {
    return false;
  }
  if (out.activeTab >= NavigationSnapshot::kTabCount ||
      out.lastBrowseTab >= NavigationSnapshot::kTabCount ||
      !collection::isValidCollection(out.collection)) {
    return false;
  }
  for (auto &stack : out.stacks) {
    uint8_t depth;
    if (!in.u8(depth) || depth > NavigationSnapshot::kMaxDepth) return false;
    stack.resize(depth);
    for (auto &entry : stack) {
      if (!in.u8(entry.kind) || !in.str(entry.key) || !in.str(entry.subKey)) {
        return false;
      }
    }
  }
  return true;
}

bool readMusic(Reader &in, PlaybackSnapshot &out) {
  uint8_t shuffle;
  if (!in.u8(out.scope) || !in.str(out.trackPath) || !in.u8(shuffle) ||
      !in.u32(out.filePosition) || !in.u32(out.elapsedSeconds) ||
      !in.u8(out.collection)) {
    return false;
  }
  if (shuffle > 1) return false;
  if (!collection::isValidCollection(out.collection)) return false;
  out.shuffle = shuffle == 1;
  return true;
}

}  // namespace

std::vector<uint8_t> ResumeCodec::encode(const ResumeRecord &record) {
  Writer payload;
  if (record.navigation) {
    const NavigationSnapshot &nav = *record.navigation;
    Writer body;
    body.u8(nav.activeTab);
    body.u8(nav.lastBrowseTab);
    body.u8(nav.collection);
    for (const auto &stack : nav.stacks) {
      if (stack.size() > NavigationSnapshot::kMaxDepth) return {};
      body.u8(static_cast<uint8_t>(stack.size()));
      for (const auto &entry : stack) {
        body.u8(entry.kind);
        body.str(entry.key);
        body.str(entry.subKey);
      }
    }
    writeSection(payload, kTagNavigation, body);
  }
  if (record.music) {
    const PlaybackSnapshot &music = *record.music;
    Writer body;
    body.u8(music.scope);
    body.str(music.trackPath);
    body.u8(music.shuffle ? 1 : 0);
    body.u32(music.filePosition);
    body.u32(music.elapsedSeconds);
    body.u8(music.collection);
    writeSection(payload, kTagMusic, body);
  }
  if (!payload.ok || kHeaderSize + payload.bytes.size() > kMaxEncodedSize) {
    return {};
  }

  Writer out;
  for (uint8_t b : kMagic) out.u8(b);
  out.u8(kVersion);
  out.u16(static_cast<uint16_t>(payload.bytes.size()));
  out.u32(crc32(payload.bytes.data(), payload.bytes.size()));
  out.bytes.insert(out.bytes.end(), payload.bytes.begin(), payload.bytes.end());
  return out.bytes;
}

bool ResumeCodec::decode(const std::vector<uint8_t> &bytes, ResumeRecord &out) {
  out = ResumeRecord{};
  if (bytes.size() < kHeaderSize || bytes.size() > kMaxEncodedSize) return false;
  Reader header(bytes.data(), kHeaderSize);
  uint8_t magic[4], version;
  uint16_t payloadLength;
  uint32_t crc;
  for (uint8_t &b : magic) header.u8(b);
  if (!header.u8(version) || !header.u16(payloadLength) || !header.u32(crc)) {
    return false;
  }
  for (int i = 0; i < 4; ++i) {
    if (magic[i] != kMagic[i]) return false;
  }
  const uint8_t *payload = bytes.data() + kHeaderSize;
  if (version != kVersion || payloadLength != bytes.size() - kHeaderSize ||
      crc != crc32(payload, payloadLength)) {
    return false;
  }

  ResumeRecord record;
  Reader in(payload, payloadLength);
  while (!in.atEnd()) {
    uint8_t tag;
    uint16_t length;
    Reader body(nullptr, 0);
    if (!in.u8(tag) || !in.u16(length) || !in.sub(length, body)) return false;
    // A repeated section is as untrustworthy as a malformed one.
    if (tag == kTagNavigation) {
      NavigationSnapshot nav;
      if (record.navigation || !readNavigation(body, nav)) return false;
      record.navigation = std::move(nav);
    } else if (tag == kTagMusic) {
      PlaybackSnapshot music;
      if (record.music || !readMusic(body, music)) return false;
      record.music = std::move(music);
    }
  }
  out = std::move(record);
  return true;
}

uint32_t ResumeCodec::crc32(const uint8_t *data, std::size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

}  // namespace knobify::resume
