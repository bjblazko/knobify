# Native formats and USB drive mode — plan

Goal: get music onto the built-in SD card without opening the housing and
without transcoding (proposal "A": smart player, plain cable). Feasibility
was measured on the device on 2026-09-15 with the throwaway firmware on
branch `experiment/format-feasibility` (results: ADR 0016).

Auto-approved by the user (away); implemented on the local branch
`feature/native-formats-usb-drive`.

## Phase 1 — play the library as it is (M4A/AAC)

1. **`library::Mp4Parser`** (host-tested): walk MP4 atoms without loading
   the file; `ilst` tags (`©nam`, `©ART`, `aART`, `©alb`, `trkn`, `disk`,
   `©day`), JPEG `covr` offset/length, exact duration (`mvhd`), `mdat`
   byte range.
2. **TagReader / listers**: dispatch `.m4a` to Mp4Parser; accept `m4a` in
   `FolderBrowser` and `SdFileLister`.
3. **Driver**: exact M4A duration and seek bitrate from Mp4Parser (the
   library's own M4A duration is an estimate, 256 s vs. 298 s measured).
4. **Covers**: replace TJpg_Decoder with a JPEGDEC adapter — baseline with
   integer downscale, progressive via JPEGDEC's DC-only 1/8 decode (19 ms
   and ~11 KB for 600 px, 234 ms for 3000 px on the device), then
   area/bilinear resample to 96 px.
5. Docs: ADR 0016, AGENTS.md gotchas, README backlog.

## Phase 2 — USB drive mode

6. Switch the S3 to TinyUSB (`ARDUINO_USB_MODE=0`, CDC stays `Serial`) and
   add a mass-storage LUN over the raw SD card (`sdmmc_read/write_sectors`
   through a DMA bounce buffer).
7. `Settings > USB drive`: stop playback, stop all SD access, export the
   card; a full-screen state says so; eject or unplug ends it, the card is
   remounted and the library rescanned.
8. Flashing: `flash-primary-mcu.sh` does the 1200-baud touch that reboots
   a TinyUSB build into the ROM bootloader.

## Known limits (go to README backlog)

- macOS reads the whole FAT on mount at ~0.87 MB/s: a 32 GB card with 4 KB
  clusters (31 MB FAT) times out, 32 KB clusters (3.9 MB) mount in 6 s.
  Cards for USB drive mode need FAT32 with 32 KB clusters.
- Ogg Vorbis is not decoded by ESP32-audioI2S 2.3.0 (stb_vorbis runs at
  2.5x realtime on the device — a second decode path, later).
- FLAC: 16-bit only in the library; tags/duration not parsed yet.
