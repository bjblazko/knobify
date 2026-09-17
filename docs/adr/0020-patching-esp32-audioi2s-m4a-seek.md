# 0020: Patching ESP32-audioI2S at build time to fix M4A seeking

## Status

Accepted — 2026-09-17. Amends ADR 0001's "pinned release, unmodified"
reading of the dependency and triggers ADR 0017's "revisit when" item 3.

## Context

Fast-forwarding or rewinding an M4A sent the audio back to 0:00 on every
cue cycle while the readout and the progress ring kept counting, so sound
and picture drifted apart permanently after the first cue. The suspicion
was that the Ogg Vorbis work (ADR 0017) had broken it. It had not:

- the library branch of `Esp32AudioI2SDriver::seekByMs()` — the one M4A
  takes — is byte-identical to its pre-Ogg version; the Vorbis work only
  added an `if (vorbisActive_)` branch above it, and
- `esphome/ESP32-audioI2S@2.3.0` has never changed in `platformio.ini`.

M4A seeking had never worked. It only became visible once ADR 0016 gave
M4A an exact duration and a true average bitrate, which made the readout
precise enough for the mismatch to be obvious.

The bug is in the pinned library. AAC in an MP4 has no syncword, so an
arbitrary byte offset can land mid-frame and crash the decoder; the
library therefore routes every M4A position — `setFilePos()` from
jog/shuttle, and `connecttoFS(fs, path, resumeFilePos)` from resume and
bookmarks — through `m4a_correctResumeFilePos()`, which walks the `stsz`
sample-size table to snap to a real frame boundary. That function opens
with a guard:

```cpp
if(!m_stsz_position) return m_audioDataStart; // guard
```

`m_stsz_position` is written only by `Audio::seek_m4a_stsz()`, and in
2.3.0 that function is never called — the whole library contains its
declaration (`Audio.h:291`) and its definition (`Audio.cpp:5249`) and no
call site. The guard therefore always fires and every M4A position is
silently rewritten to the first byte of audio. MP3 (syncword rescan) and
WAV (4-byte align) take other branches, which is why only M4A showed it.
The same guard is why resuming an M4A from a bookmark always came back at
0:00.

## Decision

Insert the missing `seek_m4a_stsz()` call into the installed library
before each build, from `scripts/patch-audioi2s.py`, wired in as
`extra_scripts = pre:scripts/patch-audioi2s.py`. The call goes at the end
of `read_M4A_Header()`'s `M4A_AMRDY` step, where the header is parsed and
`m_audioDataStart` is known, guarded to local files and wrapped in a
save/restore of the file position — `seek_m4a_stsz()` walks the atom tree
with `audiofile.seek()` and leaves the pointer at 0, which would
otherwise make the decoder read the header back as audio.

The script is idempotent (PlatformIO re-runs extra scripts on every
build) and **fails the build** if its anchor text is not found exactly
once, so a dependency change can never quietly drop the fix and leave M4A
seeking broken again.

No knobify C++ changed. `seekByMs()`, `Mp4Parser`, `Shuttle` and
`PlaybackStateMachine` were computing a correct byte target all along;
the library was discarding it.

### Rejected

- **Vendoring the library** into `lib/`, as ADR 0017 did with
  stb_vorbis. stb_vorbis is one self-contained file; ESP32-audioI2S is
  ~5000 lines of `Audio.cpp` plus three decoder trees, and taking over
  its maintenance for a one-line fix is the wrong trade.
- **Bumping the version.** Upstream master could not be confirmed to fix
  it, and 2.3.0 is pinned deliberately: knobify already works around its
  `audio_info()` null-deref and its `playSample()` 6 dB halving, and 3.x
  needs the Arduino-ESP32 3.x migration ADR 0017 describes.
- **Computing the frame boundary in knobify** and passing it in. The
  library's correction is unconditional for `CODEC_M4A`, so a
  pre-corrected offset would be discarded too. It stays available as the
  follow-up below.

## Consequences

- The dependency is no longer used verbatim. `platformio.ini` and the
  script both say so, and the build fails loudly rather than silently
  reverting — but anyone reading `.pio/libdeps` is reading patched code.
- `m4a_correctResumeFilePos()` walks `stsz` from the first sample on
  every seek, four bytes at a time via single `audiofile.read()` calls.
  A 5-minute AAC is ~12 900 frames, so a seek costs ~51 600 one-byte SD
  reads, and jog/shuttle cues every 600 ms (`Shuttle::kCycleMs`). If that
  proves too slow on the device, the follow-up is for knobify to build
  the cumulative `stsz` offset table itself in
  `Esp32AudioI2SDriver::readTrackTiming()` — which already opens the file
  through `Mp4Parser` — and to extend this patch to accept a
  pre-corrected position instead of recomputing one.
- This is the second time ESP32-audioI2S 2.3.0 has cost real debugging
  time (after the `audio_info()` crash, 2026-09-16), which is exactly
  ADR 0017's "revisit when" item 3. It is not enough on its own to justify
  the Arduino-ESP32 3.x migration now, but it is on the record: a third
  incident should tip it.

## Verified

Build side, 2026-09-17: the patch lands in the installed `Audio.cpp` and
compiles; a second build leaves exactly one copy; deleting
`.pio/libdeps/esp32-s3/ESP32-audioI2S` and rebuilding re-applies it from
a clean install; mangling the anchor fails the build with the intended
message. All 310 host tests still pass.
