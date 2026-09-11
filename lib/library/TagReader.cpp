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
  return result;
}

}  // namespace knobify::library
