#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace knobify::playback {

// Which decoder plays a file. ESP32-audioI2S 2.3.0 has no Vorbis decoder,
// so Ogg goes to knobify's own backend (ADR 0017). Opus would be a third
// value here and a second backend, nothing more.
enum class AudioBackendKind { Library, Vorbis };

inline AudioBackendKind backendForPath(const std::string &path) {
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos) return AudioBackendKind::Library;
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  // .oga is the same Vorbis stream under its audio-only name.
  if (ext == "ogg" || ext == "oga") return AudioBackendKind::Vorbis;
  return AudioBackendKind::Library;
}

}  // namespace knobify::playback
