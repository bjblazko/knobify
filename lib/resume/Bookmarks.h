#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace knobify::resume {

// Where one spoken-word title was left: which part was playing and how far
// into it. A "title" is an audiobook or a radio play -- one folder of
// parts, keyed by that folder's path.
struct Bookmark {
  std::string titleKey;  // The album folder, e.g. "/Audiobooks/Adams/Hitchhiker".
  std::string trackPath;
  uint32_t filePosition = 0;   // Driver byte offset, approximate.
  uint32_t elapsedSeconds = 0;

  bool operator==(const Bookmark &other) const {
    return titleKey == other.titleKey && trackPath == other.trackPath &&
           filePosition == other.filePosition &&
           elapsedSeconds == other.elapsedSeconds;
  }
};

// Per-title positions for collections whose profile sets
// `resumesWithinTitle` (ADR 0018). Distinct from the session resume in
// ResumeRecord, which restores the one thing that was playing when the
// power went: this remembers where *several* titles were, so leaving an
// audiobook to play music and coming back returns to the spot.
//
// Bounded and most-recently-used first: a device used for years should not
// grow an unbounded NVS blob, and the titles you are actually part-way
// through are the recent ones. Pure logic and its own codec, so both the
// eviction rule and the format are host-testable; persistence is a
// BlobStore write by the caller.
class Bookmarks {
 public:
  static constexpr char kKey[] = "bookmarks";
  static constexpr uint8_t kVersion = 1;
  static constexpr std::size_t kMaxEntries = 12;
  static constexpr std::size_t kMaxEncodedSize = 4096;
  static constexpr std::size_t kMaxStringLength = 512;
  // A day-long position is corrupt, not a real one (same rule the session
  // resume applies).
  static constexpr uint32_t kMaxElapsedSeconds = 24 * 60 * 60;

  // Records (or updates) where a title is, and makes it the most recent.
  void note(const Bookmark &mark) {
    if (mark.titleKey.empty() || mark.trackPath.empty()) return;
    for (std::size_t i = 0; i < marks_.size(); ++i) {
      if (marks_[i].titleKey != mark.titleKey) continue;
      if (marks_[i] == mark) {
        // Same position already at the front: nothing to write.
        if (i == 0) return;
        Bookmark moved = marks_[i];
        marks_.erase(marks_.begin() + static_cast<long>(i));
        marks_.insert(marks_.begin(), std::move(moved));
        dirty_ = true;
        return;
      }
      marks_.erase(marks_.begin() + static_cast<long>(i));
      break;
    }
    marks_.insert(marks_.begin(), mark);
    if (marks_.size() > kMaxEntries) marks_.resize(kMaxEntries);
    dirty_ = true;
  }

  bool lookup(const std::string &titleKey, Bookmark &out) const {
    for (const Bookmark &mark : marks_) {
      if (mark.titleKey == titleKey) {
        out = mark;
        return true;
      }
    }
    return false;
  }

  // Finished with a title (its last part played out), so Continue stops
  // offering to resume something that is over.
  void forget(const std::string &titleKey) {
    for (std::size_t i = 0; i < marks_.size(); ++i) {
      if (marks_[i].titleKey != titleKey) continue;
      marks_.erase(marks_.begin() + static_cast<long>(i));
      dirty_ = true;
      return;
    }
  }

  std::size_t size() const { return marks_.size(); }
  bool dirty() const { return dirty_; }
  void markSaved() { dirty_ = false; }

  std::vector<uint8_t> encode() const {
    std::vector<uint8_t> out;
    out.push_back('K');
    out.push_back('B');
    out.push_back('M');
    out.push_back('K');
    out.push_back(kVersion);
    // Oldest entries are dropped rather than the blob being refused: a
    // long path could otherwise make the whole set unsaveable.
    std::size_t count = marks_.size();
    std::vector<uint8_t> body;
    while (true) {
      body.clear();
      body.push_back(static_cast<uint8_t>(count));
      for (std::size_t i = 0; i < count; ++i) appendEntry(body, marks_[i]);
      if (out.size() + body.size() <= kMaxEncodedSize || count == 0) break;
      --count;
    }
    out.insert(out.end(), body.begin(), body.end());
    return out;
  }

  // False leaves the set untouched: a bad blob is treated as "no
  // bookmarks yet", exactly like a missing key.
  bool decode(const std::vector<uint8_t> &bytes) {
    if (bytes.size() < 6) return false;
    if (bytes[0] != 'K' || bytes[1] != 'B' || bytes[2] != 'M' ||
        bytes[3] != 'K' || bytes[4] != kVersion) {
      return false;
    }
    std::size_t pos = 5;
    const uint8_t count = bytes[pos++];
    if (count > kMaxEntries) return false;
    std::vector<Bookmark> parsed;
    parsed.reserve(count);
    for (uint8_t i = 0; i < count; ++i) {
      Bookmark mark;
      if (!readString(bytes, pos, mark.titleKey) ||
          !readString(bytes, pos, mark.trackPath) ||
          !readU32(bytes, pos, mark.filePosition) ||
          !readU32(bytes, pos, mark.elapsedSeconds)) {
        return false;
      }
      if (mark.titleKey.empty() || mark.trackPath.empty()) return false;
      if (mark.elapsedSeconds > kMaxElapsedSeconds) return false;
      parsed.push_back(std::move(mark));
    }
    if (pos != bytes.size()) return false;
    marks_ = std::move(parsed);
    dirty_ = false;
    return true;
  }

 private:
  static void appendU32(std::vector<uint8_t> &out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
  }

  static void appendString(std::vector<uint8_t> &out, const std::string &s) {
    const uint16_t length = static_cast<uint16_t>(
        s.size() > kMaxStringLength ? kMaxStringLength : s.size());
    out.push_back(static_cast<uint8_t>(length & 0xFF));
    out.push_back(static_cast<uint8_t>(length >> 8));
    out.insert(out.end(), s.begin(), s.begin() + length);
  }

  static void appendEntry(std::vector<uint8_t> &out, const Bookmark &mark) {
    appendString(out, mark.titleKey);
    appendString(out, mark.trackPath);
    appendU32(out, mark.filePosition);
    appendU32(out, mark.elapsedSeconds);
  }

  static bool readU32(const std::vector<uint8_t> &bytes, std::size_t &pos,
                      uint32_t &out) {
    if (pos + 4 > bytes.size()) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) {
      out |= static_cast<uint32_t>(bytes[pos + static_cast<std::size_t>(i)])
             << (8 * i);
    }
    pos += 4;
    return true;
  }

  static bool readString(const std::vector<uint8_t> &bytes, std::size_t &pos,
                         std::string &out) {
    if (pos + 2 > bytes.size()) return false;
    const std::size_t length =
        static_cast<std::size_t>(bytes[pos]) |
        (static_cast<std::size_t>(bytes[pos + 1]) << 8);
    pos += 2;
    if (length > kMaxStringLength || pos + length > bytes.size()) return false;
    out.assign(reinterpret_cast<const char *>(bytes.data() + pos), length);
    pos += length;
    return true;
  }

  std::vector<Bookmark> marks_;  // Most recently noted first.
  bool dirty_ = false;
};

}  // namespace knobify::resume
