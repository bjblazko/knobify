// TEMPORARY DIAGNOSTIC (2026-09-12): ESP32-audioI2S calls this global
// function (a convention from the library's own examples) with
// human-readable status/error strings for everything it does
// internally -- connection attempts, codec detection, stream errors.
// Without defining it, all of that is silently discarded. Investigating
// "play does nothing, no sound" reports on real hardware -- see
// AGENTS.md. Remove (or gate behind a verbose-logging flag) once
// hardware playback is confirmed working.
#include <Arduino.h>

void audio_info(const char *info) {
  Serial.print("[audio_info] ");
  Serial.println(info);
}
