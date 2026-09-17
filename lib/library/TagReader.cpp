#include "TagReader.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "Id3v2Parser.h"
#include "Mp4Parser.h"
#include "RiffInfoParser.h"
#include "Utf8.h"
#include "VorbisCommentParser.h"

namespace knobify::library {

namespace {

std::string extensionOf(const std::string &path) {
  auto dot = path.find_last_of('.');
  if (dot == std::string::npos) {
    return "";
  }
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  return ext;
}

std::string filenameWithoutExtension(const std::string &path) {
  auto slash = path.find_last_of('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  auto dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    name = name.substr(0, dot);
  }
  return name;
}

// Name of the directory directly containing `path` (i.e. the album
// folder, for a file laid out as .../Artist/Album/track.mp3) -- used to
// fall back to a folder-name-embedded year ("1998 Two Pages") when no
// tag provides one.
std::string parentFolderName(const std::string &path) {
  auto lastSlash = path.find_last_of('/');
  if (lastSlash == std::string::npos) return "";
  auto secondLastSlash =
      (lastSlash == 0) ? std::string::npos : path.find_last_of('/', lastSlash - 1);
  size_t start = (secondLastSlash == std::string::npos) ? 0 : secondLastSlash + 1;
  return path.substr(start, lastSlash - start);
}

uint16_t parseLeadingYearValue(const std::string &text);

// The folder names between the library root and the file itself, for a
// path like "/Music/Air/1998 Moon Safari/02 Sexy Boy.mp3" -> {"Air",
// "1998 Moon Safari"}. The first component is the scan root and carries
// no meaning about the music, so it is dropped.
std::vector<std::string> foldersBelowRoot(const std::string &path) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start < path.size()) {
    size_t slash = path.find('/', start);
    if (slash == std::string::npos) break;  // The rest is the file name.
    if (slash > start) parts.push_back(path.substr(start, slash - start));
    start = slash + 1;
  }
  if (!parts.empty()) parts.erase(parts.begin());  // The scan root.
  return parts;
}

// "1998 Moon Safari" -> "Moon Safari"; text without a leading year is
// returned unchanged. The year itself is read separately by
// parseLeadingYearValue().
std::string withoutLeadingYear(const std::string &text) {
  if (parseLeadingYearValue(text) == 0) return text;
  size_t start = 4;
  while (start < text.size() &&
         (text[start] == ' ' || text[start] == '-' || text[start] == '_')) {
    ++start;
  }
  return start < text.size() ? text.substr(start) : text;
}

// A plausible 4-digit year at the very start of `text` (e.g. the "1998"
// in "1998 Two Pages"), or 0 if it doesn't start with one.
uint16_t parseLeadingYearValue(const std::string &text) {
  if (text.size() < 4) return 0;
  for (int i = 0; i < 4; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) return 0;
  }
  int year = std::stoi(text.substr(0, 4));
  return (year >= 1000 && year <= 2999) ? static_cast<uint16_t>(year) : 0;
}

struct FilenameNumbers {
  uint16_t disc = 0;
  uint16_t track = 0;
};

// Leading numbers from a filename like "07 Song.mp3" (-> track 7) or a
// disc-qualified "2-01 Song.mp3" (-> disc 2, track 1) -- used when no tag
// provides them.
FilenameNumbers parseLeadingNumbersFromFilename(const std::string &path) {
  std::string name = filenameWithoutExtension(path);

  auto readDigits = [&](size_t &i) -> long {
    size_t start = i;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) {
      ++i;
    }
    return (i == start) ? -1 : std::stol(name.substr(start, i - start));
  };

  size_t i = 0;
  FilenameNumbers numbers;
  long first = readDigits(i);
  if (first < 0) return numbers;
  if (i < name.size() && name[i] == '-') {
    size_t afterDash = i + 1;
    long second = readDigits(afterDash);
    if (second >= 0) {
      numbers.disc = static_cast<uint16_t>(first);
      numbers.track = static_cast<uint16_t>(second);
      return numbers;
    }
  }
  numbers.track = static_cast<uint16_t>(first);
  return numbers;
}

}  // namespace

TagResult TagReader::read(RawFile &file, const std::string &filePath) {
  std::string ext = extensionOf(filePath);
  TagResult result;

  if (ext == "mp3") {
    result = Id3v2Parser::parse(file);
  } else if (ext == "m4a") {
    result = Mp4Parser::parse(file).tags;
  } else if (ext == "ogg") {
    result = VorbisCommentParser::parse(file);
  } else if (ext == "wav") {
    result = RiffInfoParser::parse(file);
  }

  // Untagged files still sit in a folder tree that says what they are, so
  // borrow those names rather than filling the library with "Unknown
  // Artist" (user request, 2026-09-16 -- audio dramas and home recordings
  // often carry no tags at all). Two folders below the root read as
  // Artist/Album; a single folder names both, so one untagged series
  // folder becomes one findable album rather than a pile of unknowns.
  const std::vector<std::string> folders = foldersBelowRoot(filePath);
  if (result.artist.empty() && !folders.empty()) {
    result.artist = folders.size() >= 2 ? folders[folders.size() - 2]
                                        : withoutLeadingYear(folders.back());
  }
  if (result.album.empty() && !folders.empty()) {
    result.album = withoutLeadingYear(folders.back());
  }
  if (result.artist.empty()) {
    result.artist = "Unknown Artist";
  }
  if (result.album.empty()) {
    result.album = "Unknown Album";
  }
  if (result.title.empty()) {
    result.title = filenameWithoutExtension(filePath);
  }
  if (result.trackNumber == 0 || result.discNumber == 0) {
    FilenameNumbers numbers = parseLeadingNumbersFromFilename(filePath);
    if (result.trackNumber == 0) result.trackNumber = numbers.track;
    if (result.discNumber == 0) result.discNumber = numbers.disc;
  }
  if (result.year == 0) {
    result.year = parseLeadingYearValue(parentFolderName(filePath));
  }

  // Single choke point for "is this actually UTF-8", run after every
  // other source (tag parser, folder names, filename) has had a chance
  // to fill in title/artist/album. Vorbis comments and MP4 atoms are
  // supposed to already be UTF-8, but VorbisCommentParser and
  // RiffInfoParser don't transcode (RIFF INFO text is conventionally
  // CP1252/Latin-1-ish, never declared), and the SD-path fallbacks above
  // copy folder/filename bytes verbatim -- so nothing upstream of here
  // is guaranteed valid. repair() is a no-op for text that's already
  // good UTF-8, and downgrades anything else from Latin-1 rather than
  // ever handing LVGL invalid UTF-8.
  result.title = utf8::repair(result.title);
  result.artist = utf8::repair(result.artist);
  result.album = utf8::repair(result.album);
  // Genres come from the same untrusted sources -- and a genre shelf is
  // built from these strings, so one bad byte would name a whole shelf.
  result.genre = utf8::repair(result.genre);
  return result;
}

}  // namespace knobify::library
