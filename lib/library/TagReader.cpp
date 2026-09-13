#include "TagReader.h"

#include <algorithm>
#include <cctype>

#include "Id3v2Parser.h"
#include "RiffInfoParser.h"
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

// A plausible 4-digit year at the very start of `text` (e.g. the "1998"
// in "1998 Two Pages"), or 0 if it doesn't start with one.
uint16_t parseLeadingYear(const std::string &text) {
  if (text.size() < 4) return 0;
  for (int i = 0; i < 4; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) return 0;
  }
  int year = std::stoi(text.substr(0, 4));
  return (year >= 1000 && year <= 2999) ? static_cast<uint16_t>(year) : 0;
}

// A leading track number from a filename like "07 Song.mp3" (-> 7) or a
// disc-qualified "2-01 Song.mp3" (-> 1, the track-within-disc number, not
// the disc number) -- used when no tag provides a track number.
uint16_t parseLeadingTrackNumberFromFilename(const std::string &path) {
  std::string name = filenameWithoutExtension(path);

  auto readDigits = [&](size_t &i) -> long {
    size_t start = i;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) {
      ++i;
    }
    return (i == start) ? -1 : std::stol(name.substr(start, i - start));
  };

  size_t i = 0;
  long first = readDigits(i);
  if (first < 0) return 0;
  if (i < name.size() && name[i] == '-') {
    size_t afterDash = i + 1;
    long second = readDigits(afterDash);
    if (second >= 0) return static_cast<uint16_t>(second);
  }
  return static_cast<uint16_t>(first);
}

}  // namespace

TagResult TagReader::read(RawFile &file, const std::string &filePath) {
  std::string ext = extensionOf(filePath);
  TagResult result;

  if (ext == "mp3") {
    result = Id3v2Parser::parse(file);
  } else if (ext == "ogg") {
    result = VorbisCommentParser::parse(file);
  } else if (ext == "wav") {
    result = RiffInfoParser::parse(file);
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
  if (result.trackNumber == 0) {
    result.trackNumber = parseLeadingTrackNumberFromFilename(filePath);
  }
  if (result.year == 0) {
    result.year = parseLeadingYear(parentFolderName(filePath));
  }
  return result;
}

}  // namespace knobify::library
