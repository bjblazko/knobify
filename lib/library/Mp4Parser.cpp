#include "Mp4Parser.h"

#include <cstring>
#include <string>

#include "Id3Genres.h"
#include "Utf8.h"

namespace knobify::library {

namespace {

// Longest tag string kept; longer values are truncated, not rejected.
constexpr size_t kMaxTextBytes = 512;
// moov > udta > meta > ilst > item > data is the deepest path needed.
constexpr int kMaxDepth = 6;

// `covr` data atom type codes (QuickTime "well-known types").
constexpr uint32_t kTypeJpeg = 13;

uint32_t be32(const uint8_t *b) {
  return (static_cast<uint32_t>(b[0]) << 24) |
         (static_cast<uint32_t>(b[1]) << 16) |
         (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

uint64_t be64(const uint8_t *b) {
  return (static_cast<uint64_t>(be32(b)) << 32) | be32(b + 4);
}

constexpr uint32_t fourcc(const char s[5]) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
}

// iTunes item names start with the byte 0xA9 ("©").
constexpr uint32_t kNam = 0xA96E616D;  // ©nam
constexpr uint32_t kArt = 0xA9415254;  // ©ART
constexpr uint32_t kAlb = 0xA9616C62;  // ©alb
constexpr uint32_t kDay = 0xA9646179;  // ©day
constexpr uint32_t kGen = 0xA967656E;  // ©gen

struct Atom {
  uint32_t type = 0;
  size_t payload = 0;  // First byte after the header.
  size_t end = 0;      // One past the last byte.
};

// Reads the atom header at `pos`, bounded by `limit`. False when there's
// no complete, sane atom there.
bool readAtom(RawFile &file, size_t pos, size_t limit, Atom *atom) {
  uint8_t header[16];
  if (pos + 8 > limit || !file.seek(pos) || file.read(header, 8) != 8) {
    return false;
  }
  uint64_t size = be32(header);
  size_t headerLen = 8;
  if (size == 1) {
    if (pos + 16 > limit || file.read(header + 8, 8) != 8) return false;
    size = be64(header + 8);
    headerLen = 16;
  } else if (size == 0) {
    size = limit - pos;  // Extends to the end of its container.
  }
  if (size < headerLen || size > limit - pos) return false;
  atom->type = be32(header + 4);
  atom->payload = pos + headerLen;
  atom->end = pos + static_cast<size_t>(size);
  return true;
}

// Payload of an item's `data` child: its offset and length after the
// 8-byte type/locale prefix, and its type code.
bool findData(RawFile &file, const Atom &item, size_t *offset, size_t *length,
              uint32_t *type) {
  Atom child;
  for (size_t pos = item.payload; readAtom(file, pos, item.end, &child);
       pos = child.end) {
    if (child.type != fourcc("data")) continue;
    uint8_t prefix[8];
    if (child.end - child.payload < 8 || !file.seek(child.payload) ||
        file.read(prefix, 8) != 8) {
      return false;
    }
    *type = be32(prefix) & 0x00FFFFFF;
    *offset = child.payload + 8;
    *length = child.end - *offset;
    return true;
  }
  return false;
}

std::string readText(RawFile &file, size_t offset, size_t length) {
  // Read a few bytes past kMaxTextBytes so a raw cut that lands mid a
  // multi-byte UTF-8 sequence still leaves utf8::truncate() a complete
  // sequence to inspect at the boundary, rather than hard-cutting
  // exactly at the cap (which utf8::truncate can't fix up after the
  // fact, since by then the missing continuation bytes are already
  // gone).
  size_t readLen = length;
  if (readLen > kMaxTextBytes + 3) readLen = kMaxTextBytes + 3;
  std::string text(readLen, '\0');
  if (!file.seek(offset)) return "";
  text.resize(file.read(reinterpret_cast<uint8_t *>(&text[0]), readLen));
  auto nul = text.find('\0');
  if (nul != std::string::npos) text.resize(nul);
  // iTunes/MP4 text atoms are documented as UTF-8, but repair() guards
  // against a file that wrote Latin-1 there anyway; truncate() then
  // enforces the byte cap without ever splitting a sequence.
  return utf8::truncate(utf8::repair(text), kMaxTextBytes);
}

// trkn/disk: 2 reserved bytes, then the number, then the total.
uint16_t readIndexNumber(RawFile &file, size_t offset, size_t length) {
  uint8_t b[4];
  if (length < 4 || !file.seek(offset) || file.read(b, 4) != 4) return 0;
  return static_cast<uint16_t>((b[2] << 8) | b[3]);
}

uint16_t parseYear(const std::string &day) {
  if (day.size() < 4) return 0;
  uint16_t year = 0;
  for (size_t i = 0; i < 4; ++i) {
    if (day[i] < '0' || day[i] > '9') return 0;
    year = static_cast<uint16_t>(year * 10 + (day[i] - '0'));
  }
  return year;
}

void parseIlst(RawFile &file, const Atom &ilst, Mp4Info *info) {
  std::string albumArtist;
  Atom item;
  for (size_t pos = ilst.payload; readAtom(file, pos, ilst.end, &item);
       pos = item.end) {
    size_t offset = 0;
    size_t length = 0;
    uint32_t type = 0;
    if (!findData(file, item, &offset, &length, &type)) continue;
    TagResult &tags = info->tags;
    switch (item.type) {
      case kNam: tags.title = readText(file, offset, length); break;
      case kArt: tags.artist = readText(file, offset, length); break;
      case kAlb: tags.album = readText(file, offset, length); break;
      case kDay: tags.year = parseYear(readText(file, offset, length)); break;
      // Free-text genre; the numbered `gnre` below only fills in when
      // this is absent, since iTunes writes one or the other.
      case kGen: tags.genre = readText(file, offset, length); break;
      default:
        if (item.type == fourcc("aART")) {
          albumArtist = readText(file, offset, length);
        } else if (item.type == fourcc("trkn")) {
          tags.trackNumber = readIndexNumber(file, offset, length);
        } else if (item.type == fourcc("disk")) {
          tags.discNumber = readIndexNumber(file, offset, length);
        } else if (item.type == fourcc("gnre") && tags.genre.empty()) {
          // A big-endian index into the ID3v1 table, one-based here.
          uint8_t b[2];
          if (length >= 2 && file.seek(offset) && file.read(b, 2) == 2) {
            const unsigned index = static_cast<unsigned>((b[0] << 8) | b[1]);
            const char *name = index > 0 ? id3v1Genre(index - 1) : nullptr;
            if (name) tags.genre = name;
          }
        } else if (item.type == fourcc("covr") && !tags.picture.present) {
          uint8_t magic[2] = {0, 0};
          bool jpeg = type == kTypeJpeg;
          if (!jpeg && length >= 2 && file.seek(offset) &&
              file.read(magic, 2) == 2) {
            jpeg = magic[0] == 0xFF && magic[1] == 0xD8;  // Untyped JPEG.
          }
          if (jpeg && length > 0) {
            tags.picture.present = true;
            tags.picture.offset = offset;
            tags.picture.length = length;
          }
        }
        break;
    }
  }
  // Like ID3's TPE1: the track artist, with the album artist as fallback.
  if (info->tags.artist.empty()) info->tags.artist = albumArtist;
}

// mvhd: version, 3 flag bytes, then creation/modification times, timescale
// and duration -- 32-bit fields in version 0, 64-bit times in version 1.
void parseMvhd(RawFile &file, const Atom &mvhd, Mp4Info *info) {
  uint8_t b[32];
  size_t available = mvhd.end - mvhd.payload;
  if (!file.seek(mvhd.payload) || available < 20 || file.read(b, 1) != 1) {
    return;
  }
  uint32_t timescale = 0;
  uint64_t duration = 0;
  if (b[0] == 1) {
    if (available < 32 || file.read(b + 1, 31) != 31) return;
    timescale = be32(b + 20);
    duration = be64(b + 24);
  } else {
    if (file.read(b + 1, 19) != 19) return;
    timescale = be32(b + 12);
    duration = be32(b + 16);
  }
  if (timescale == 0) return;
  info->durationMs = static_cast<uint32_t>(duration * 1000 / timescale);
}

void walk(RawFile &file, size_t start, size_t end, int depth, Mp4Info *info) {
  if (depth >= kMaxDepth) return;
  Atom atom;
  for (size_t pos = start; readAtom(file, pos, end, &atom); pos = atom.end) {
    switch (atom.type) {
      case fourcc("moov"):
      case fourcc("udta"):
        walk(file, atom.payload, atom.end, depth + 1, info);
        break;
      case fourcc("meta"):
        // A full box: version and flags before its children.
        walk(file, atom.payload + 4, atom.end, depth + 1, info);
        break;
      case fourcc("ilst"):
        parseIlst(file, atom, info);
        break;
      case fourcc("mvhd"):
        parseMvhd(file, atom, info);
        break;
      case fourcc("mdat"):
        if (depth == 0) {
          info->mdatStart = atom.payload;
          info->mdatEnd = atom.end;
        }
        break;
      default:
        break;
    }
  }
}

}  // namespace

Mp4Info Mp4Parser::parse(RawFile &file) {
  Mp4Info info;
  walk(file, 0, file.size(), 0, &info);
  const TagResult &tags = info.tags;
  info.tags.found = !tags.title.empty() || !tags.artist.empty() ||
                    !tags.album.empty() || tags.trackNumber != 0;
  return info;
}

}  // namespace knobify::library
