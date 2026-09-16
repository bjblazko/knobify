# 0017: Two audio decode paths

## Status

Accepted — 2026-09-16. Follows ADR 0016 (native formats) and records the
debt that Ogg Vorbis support creates.

## Context

ESP32-audioI2S 2.3.0 decodes MP3, AAC/M4A, WAV and FLAC, but has no Vorbis
decoder: it rejects Ogg streams that aren't Ogg-FLAC. The user's audio
dramas — 1.5 GB, 29 files of about 23 minutes — are Vorbis, and a player
published as open source should handle a free format.

The library that does decode Vorbis is upstream ESP32-audioI2S 3.x, which
requires Arduino-ESP32 3.x / ESP-IDF 5. This board fought us on nearly
every layer that migration would touch: octal PSRAM init, the SDMMC card
layout, TinyUSB mass storage, the USB CDC console, touch calibration, a
non-standard rotary encoder, and a display driver vendored against
Arduino_GFX 1.4.9 because newer releases need the 3.x core. All of it
would need re-validating for one format.

## Decision

Keep the library for its formats and add a second decode path for Vorbis:
stb_vorbis behind `DecoderBackend`, decoding on its own task and writing
through `AudioOutputStage`, the single place where samples reach the DAC.
`Esp32AudioI2SDriver` picks the path by file extension
(`playback::backendForPath()`) and keeps the two mutually exclusive.

## Consequences

- Two decode paths to keep in step. `AudioGain` (host-tested) and
  `AudioOutputStage` are the guard: volume, the sleep-timer fade and the
  spectrum come from one implementation, so the paths cannot drift in
  loudness or in what the analyzer sees.
- Ogg positions are sample indices, not byte offsets. The value stays
  opaque above `PlaybackDriver`, so resume and jog/shuttle work unchanged.
- While an Ogg plays, ~180 KB of PSRAM and a 32 KB task stack are in use;
  both are released when it stops. PSRAM is shared with the LVGL pool.
- Opus is a new backend, not a new structure — but see below.

## Revisit when

1. **Opus is wanted.** A third decode path would make this a pattern
   rather than an exception — that is the point to move to a library that
   decodes everything.
2. **The project moves to Arduino-ESP32 3.x for any other reason.** The
   upgrade to ESP32-audioI2S 3.x is nearly free then, and this debt should
   be paid in the same migration rather than left behind it.
3. **ESP32-audioI2S 2.3.0 costs us again.** It already called a weak hook
   without a null check and crashed M4A playback (2026-09-16, AGENTS.md).
   A second incident of that kind is reason enough.
4. **Yearly**, whichever comes first — next review due 2027-09.
