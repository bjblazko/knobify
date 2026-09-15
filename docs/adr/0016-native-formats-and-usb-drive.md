# 0016: Native M4A, progressive covers and a USB drive mode

## Status

Proposed — 2026-09-16. Implemented on branch `feature/native-formats-usb-drive`
while the user was away (they had approved "solution A"). **Reverses ADR
0002's decision not to decode AAC/M4A natively**; the licensing concern
behind that decision needs the user's confirmation before this is accepted.

## Context

The SD card is built into the housing. Getting music onto it meant opening
the case, and the library had to be transcoded first: it's 29 GB of
iTunes-style M4A (all AAC-LC, mostly 256 kbps; no ALAC in a 126-file
sample) whose embedded covers are 92% progressive JPEG, which
TJpg_Decoder can't read. Transcoding lossy AAC to MP3 loses quality and
saves no space. Audiobooks (1.5 GB) are Ogg Vorbis.

Three directions were weighed: a smarter player with USB cable transfer
(A), a desktop companion app that transcodes and syncs (B), or the device
pulling from Jellyfin over WiFi (C). The user wants a standalone player
to carry around, published on GitHub for people with plain local files,
and chose A.

## Feasibility (measured on the device, 2026-09-15)

Throwaway firmware on branch `experiment/format-feasibility`:

- **AAC/M4A playback**: ESP32-audioI2S 2.3.0 already contains an AAC
  decoder. 256 kbps M4A loads the audio core about as much as 320 kbps
  MP3 (60% vs. 64% by an idle counter, which includes the library's
  per-sample I2S writes), no gaps over 50 ms after start. Files with
  `moov` after `mdat` and 3000 px covers play. Byte seeks
  (`setFilePos`) work; the library's own time seek refuses M4A and its
  duration is an estimate (256 s for a 298 s track).
- **FLAC**: 16-bit plays (58% core); 24-bit is refused by the library.
- **Ogg Vorbis**: not decoded by 2.3.0. stb_vorbis decodes 2.5x realtime
  on one core (~40%) — feasible, but a second decode path.
- **Progressive JPEG**: stb_image decodes 600 px in 604 ms but needs
  4.2 MB of PSRAM and runs out above ~1000 px. JPEGDEC's DC-only 1/8
  decode takes 19 ms / 11 KB (600 px) and 234 ms (3000 px); at the
  96 px cover size it looks almost the same as a full decode.
- **USB mass storage**: TinyUSB MSC over raw SD sectors works at
  ~0.87 MB/s (USB full speed). macOS reads the whole FAT when mounting:
  the 32 GB card's 4 KB clusters mean a 31 MB FAT, the mount times out;
  a simulated volume with 32 KB clusters (3.9 MB FAT) mounted in 6 s.

## Decision

1. **Play M4A as it is.** `library::Mp4Parser` reads iTunes tags, the JPEG
   `covr`, the exact `mvhd` duration and the `mdat` range by seeking
   (checked against 40 real files: durations within 0.1 s of ffprobe).
   The driver uses them for the time display and seek bitrates; M4A is
   seekable like MP3.
2. **JPEGDEC replaces TJpg_Decoder** for all covers: baseline at the
   largest fitting 1/2..1/8 scale, progressive from DC coefficients at
   1/8, then `SquareResampler` (area average down, bilinear up).
3. **Settings > USB drive.** The S3 runs TinyUSB (`ARDUINO_USB_MODE=0`)
   with CDC serial plus an MSC LUN over the SD card. Starting a session
   stops playback and the sleep timer, exports the card and re-enumerates
   USB, so the computer sees a freshly plugged-in drive; eject, unplug
   (host gone 1.5 s) or Done end it, the card is remounted and the library
   rescanned. The screen is modal and asks to eject before Done.
4. **No transcoding anywhere.** Vorbis, 24-bit FLAC and anything else the
   library can't decode stay out for now (README backlog).

## Consequences

- The library plays from the card as it is; covers need no re-encoding
  (`scripts/convert-music-library.sh` is no longer required for M4A).
- **Cards for USB drive mode need FAT32 with 32 KB clusters** on macOS.
  128 GB cards would still be borderline (15.6 MB FAT at 32 KB). exFAT
  (small allocation bitmap) would avoid this but isn't in the prebuilt
  ESP-IDF FatFs, and the project decided earlier to stay on FAT32.
- A first fill of 30 GB over USB takes ~10 h; a card reader is faster.
- Flashing needs a 1200-baud touch (done by `flash-primary-mcu.sh`), and
  a crash's output is no longer visible over USB: use the `INFO` serial
  command and `scripts/read-coredump.sh`.
- ADR 0002's licensing reasoning no longer holds as stated. Note that the
  AAC decoder was already compiled into every firmware image before this
  change (Audio.cpp references it unconditionally); only whether knobify
  *uses and advertises* it changes.

## Open

- Mount on macOS with the screen unlocked and a 32 KB-cluster card (the
  last runs were blocked: a locked Mac ejects new removable storage).
- Windows/Linux mount behaviour with 4 KB clusters (they may not scan the
  whole FAT).
