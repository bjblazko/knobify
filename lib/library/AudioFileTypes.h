#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace knobify::library {

// Whether a file name's extension is one the library lists and the player
// is asked to play. Shared by the SD walk and the folder browser so both
// agree. m4a is AAC in MP4 (ADR 0016).
inline bool isAudioFileName(const std::string &name) {
  auto dot = name.find_last_of('.');
  if (dot == std::string::npos) return false;
  std::string ext = name.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext == "mp3" || ext == "m4a" || ext == "ogg" || ext == "wav";
}

}  // namespace knobify::library
