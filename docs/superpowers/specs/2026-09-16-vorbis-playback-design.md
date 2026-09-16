# Ogg Vorbis playback — design

## Context

knobify lists `.ogg` files but can't play them: ESP32-audioI2S 2.3.0
decodes MP3, AAC/M4A, WAV and FLAC, and rejects Ogg streams that aren't
Ogg-FLAC ("ogg/flac support only", Audio.cpp:2127). The user's audiobook
collection is 29 Ogg Vorbis files of ~23 minutes each, and a published
player that can't play a common free format is a gap.

Measured on the device 2026-09-15 (branch `experiment/format-feasibility`):
stb_vorbis decodes this material at **2.5x real time**, about 40% of one
core — lighter than the MP3 path's ~60%. Setup memory was ~180 KB; the
decoder needs a large stack (it overflowed the 8 KB loop task; 32 KB was
enough).

## Goals

- Ogg Vorbis plays with the same behaviour as MP3/M4A: exact duration,
  jog/shuttle seeking, resume after power loss, the spectrum analyser,
  identical volume steps.
- The decoder sits behind a slot that a future Opus backend can fill
  without restructuring.
- No regression risk for the existing formats: their path stays as it is.

## Non-goals

- Opus (the slot is prepared; no libopus work now).
- Cover art embedded inside Ogg files (`METADATA_BLOCK_PICTURE`). Folder
  `cover.jpg` covers already work and cover the real cases; the README
  says so rather than half-supporting it.
- Migrating to ESP32-audioI2S 3.x / Arduino-ESP32 3.x — deliberately
  deferred, recorded as debt in ADR 0017.

## Architecture

Three parts, all inside `lib/drivers-audio` (plus pure logic that moves
to host-testable headers):

### 1. `AudioOutputStage` — one place where PCM becomes sound

Owns the volume step, the sleep-timer output gain and the spectrum sample
ring, so both decode paths behave identically:

- `applyToPackedSample(uint32_t *sample)` — what the existing
  `audio_process_i2s` hook calls for the library path (undo the library's
  `>>1`, apply gain, feed the ring). Today's logic, moved.
- `writeFrames(const int16_t *interleaved, size_t frames)` — the Vorbis
  path: apply gain, feed the ring, `i2s_write` to the port the library
  already installed.

The gain maths itself (`volume step + output gain -> scale`, the 22-entry
table copied from the library) moves into `lib/playback/AudioGain.h` —
pure logic with no Arduino includes, so the native test environment can
compile it (`coding-guidelines.md`: if a unit test would need a real
ESP32, the code is in the wrong layer). `AudioOutputStage` itself stays
device-side: it writes to I2S port 0, the port the library's `Audio`
object installs in its constructor.

### 2. `DecoderBackend` — the codec slot

```cpp
class DecoderBackend {
 public:
  virtual bool open(const std::string &path, uint32_t startSample) = 0;
  virtual void close() = 0;
  virtual bool seekToSample(uint32_t sample) = 0;
  virtual uint32_t currentSample() const = 0;   // what is being heard
  virtual uint32_t sampleRate() const = 0;
  virtual uint32_t durationSeconds() const = 0;
  virtual bool running() const = 0;
  virtual void setPaused(bool paused) = 0;
};
```

`VorbisBackend` implements it with stb_vorbis (vendored as
`lib/drivers-audio/stb_vorbis.c`, public domain / MIT):

- Reads the file through the normal filesystem (`fopen` on `/sdcard/...`),
  as in the measured spike.
- Decodes on **its own FreeRTOS task**, created when an Ogg file starts
  and deleted when it stops: 32 KB stack, priority 3, core 0 (matching the
  existing audio task), so that stack is only committed while Ogg plays.
- Decodes into a small frame buffer and hands each block to
  `AudioOutputStage::writeFrames()`. The blocking `i2s_write` paces the
  loop; no extra timing logic.
- Mono files are duplicated to stereo; the I2S clock is set from the
  stream's sample rate on open.
- Keeps `currentSample_` as an atomic, updated per block.

### 3. `Esp32AudioI2SDriver` — dispatcher

Stays the only `PlaybackDriver` implementation. A pure helper
`backendForPath()` — in `lib/playback`, next to the other
extension-driven logic — picks Vorbis for `.ogg`, the library otherwise;
host-tested. Every entry point (`playFile`, `playFileAt`, `pause`, `resume`,
`stop`, `seekByMs`, `filePosition`, `durationSeconds`, `isRunning`) routes
to the active backend under the existing mutex, and starting one backend
stops the other first, so only one ever touches I2S.

## Position semantics

`PlaybackDriver`'s `uint32_t` position is a byte offset for the library
formats. For Vorbis it is a **sample index** — opaque above the driver,
and the unit stb_vorbis can seek to. 23 minutes at 44.1 kHz is 60 million
samples, far inside `uint32_t`.

| Feature | Library path | Vorbis path |
|---|---|---|
| Duration | Xing/`mvhd`/estimate | stream header, exact |
| Position | byte offset minus input buffer | current sample index |
| Seek | bytes from average bitrate | ms → samples → `stb_vorbis_seek` |
| Resume | `playFileAt(path, bytes)` | `playFileAt(path, sample)` |

`PlaybackStateMachine::canSeek()` gains `ogg` (one line, host-tested);
nothing else above the driver changes.

## Error handling

- Unopenable file, or Ogg that isn't Vorbis (e.g. an Opus stream named
  `.ogg`): `open()` fails, the driver reports not-playing and logs once —
  the same outcome as a broken MP3 today.
- Allocation failure (~180 KB PSRAM, 32 KB internal stack): the backend
  refuses to start rather than playing partially. PSRAM is shared with the
  LVGL pool, so this is a real case, not a formality.
- Slow SD reads: `i2s_write` blocks and playback stutters, as MP3 does
  today. No new watchdog behaviour.
- Never two decoders at once: enforced by the driver's mutex and by
  stopping the other backend before starting one.

## Testing

Host (Unity, `test/test_audio_backends/`, covering the pure pieces in
`lib/playback`):

- `backendForPath()` — extension mapping, case-insensitive, unknown
  extensions fall back to the library.
- `AudioGain` — volume step + output gain → scale, including unity and
  silence, and that the table matches the library's.
- Sample↔time conversions used for seeking and the readout.
- `PlaybackStateMachine::canSeek()` accepts ogg (existing test file).

Device (only when the hardware is free — the user asked for no flashing
during USB transfers):

1. A Hörspiel plays; CPU and dropout counters comparable to the spike.
2. Jog/shuttle forward and back lands where expected.
3. Resume after a reboot returns to the same position.
4. Spectrum reacts; volume steps sound like MP3 at the same setting.
5. Switching Ogg → MP3 → Ogg in one session leaves no stuck task
   (`INFO` reports stable internal heap).

## Work plan

1. Extract `AudioOutputStage` + `AudioGain` from today's driver, with host
   tests. No behaviour change — the existing formats must still play.
2. Vendor stb_vorbis; add `DecoderBackend`, `VorbisBackend` and dispatch;
   `canSeek` accepts ogg.
3. ADR 0017 (the two-decode-path debt, with revisit triggers), README and
   THIRD-PARTY updates.
4. Device verification, when the user says the device is free.

## Debt

ADR 0017 records that knobify now has two decode paths because the pinned
library can't decode Vorbis, and that the alternative — ESP32-audioI2S
3.x on Arduino-ESP32 3.x — was rejected for now because it would mean
re-validating PSRAM mode, the SDMMC quirks, TinyUSB mass storage, CDC
serial, touch, the encoder and the vendored display driver (all of which
this board made difficult once already).

Revisit when any of these happens:

1. Opus is wanted — a third path would make this a pattern, not an
   exception.
2. The project moves to Arduino-ESP32 3.x for any other reason; then this
   debt should be paid in the same migration.
3. ESP32-audioI2S 2.3.0 proves unmaintained in a way that costs us again
   (e.g. the unguarded `audio_info()` hook that crashed M4A playback,
   2026-09-16).
4. A yearly review, whichever comes first.

The ADR is linked from the audio driver's header and the README backlog,
so it is visible where the decision bites.
