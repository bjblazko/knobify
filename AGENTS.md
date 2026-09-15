# Notes for AI agents (and humans) working on this repo

This file exists specifically to stop expensive hardware-bring-up
lessons from being re-learned from scratch in a future session. Update
it whenever you find a genuinely non-obvious hardware fact or fix a bug
that only showed up on the physical board (not from compiling). Keep
entries short; link to the fuller writeup (ADR, `device.md`) instead of
duplicating it.

**Before touching hardware-facing code**, read:
- [`device.md`](device.md) — pinout, board identification, hardware
  quirks observed.
- [`docs/adr/0004-navigation-library-and-index-architecture.md`](docs/adr/0004-navigation-library-and-index-architecture.md)
  — the "Implementation status" section documents every real bug found
  during hardware bring-up, in the order it was found, with root cause
  and fix. It's long but it's the actual debugging history — read it
  before assuming something works just because it compiles.

## Hardware gotchas (quick index — see linked sections for detail)

- **Touch X is miscalibrated in the raw CST816 data** (raw ~= 1.18 *
  visual - 66), corrected by `lib/input/TouchCalibration.h` (defaults)
  or a re-fit saved from Settings > Touch calibration (NVS `touchcal`,
  ADR 0010). "Buttons miss taps" → run that calibration first (its fit
  is logged as `[touchcal]` on serial), and use `-DKNOBIFY_TOUCH_DEBUG`
  logging before touching sizes/timing. See ADR 0004.

- **Spectrum frame cost**: build with `-DKNOBIFY_SPECTRUM_DEBUG` to log
  samples/rate/gain and the worst per-frame analyzer time every 90 frames
  (~2 ms measured 2026-09-14). The driver copies ESP32-audioI2S's volume
  table to divide gain out of the tapped samples — re-check it if the
  library is upgraded. See ADR 0009.
- **This board's rotary encoder is rotation-only** (no click/push) and,
  more subtly, **is not a standard 4-state quadrature encoder** — its
  raw pin states never visit `00` (both contacts closed), only `11`
  (rest), `01`, and `10`. A decoder written against the textbook
  Gray-code model (any well-known Arduino rotary-encoder library
  included) will silently never fire on this hardware. See
  `lib/drivers-encoder/GpioEncoderDriver.h`'s class comment for the
  actual 3-state model used, and don't assume it transfers to a
  different encoder without re-capturing raw pin states first (the
  `drainLog`-style raw logging approach used to find this is worth
  repeating for any new input hardware, rather than guessing).
- **Board config must be `4d_systems_esp32s3_gen4_r8n16`, not
  `esp32-s3-devkitc-1`.** The latter is explicitly labeled "No PSRAM" in
  its own board definition; using it (even with manual `psram_type`
  overrides) causes real PSRAM init failures on this R8-variant chip,
  and it also lacks `ARDUINO_USB_CDC_ON_BOOT=1`, which silently sends
  all `Serial` output to unconnected physical UART0 pins instead of the
  native USB port this board is flashed through. See `platformio.ini`'s
  comment block and ADR 0004.
- **QSPI display must go through Arduino_GFX, not ESP-IDF's
  `esp_lcd_panel_io_spi`.** That component's QSPI (4-line) transaction
  support was added in a later ESP-IDF than this project's platform
  version bundles; it compiles and returns `ESP_OK` for everything while
  never actually driving the panel. Use `Arduino_GFX`'s
  `Arduino_ESP32QSPI` bus class instead (implements QSPI itself against
  `spi_master`), pinned to v1.4.9 specifically (newer releases need a
  header this project's Arduino-ESP32 core doesn't have). See
  `platformio.ini` and `lib/drivers-display/`.
- **This board's USB-serial reaches the primary ESP32-S3R8 two ways**:
  the documented CH340 path, or (confirmed working) the chip's own
  native USB-Serial-JTAG peripheral. `scripts/flash-primary-mcu.sh`
  auto-detects either. Cable orientation can also matter for which of
  the board's *two MCUs* (primary S3 vs. secondary U4WDH) the CH340 path
  reaches — see `device.md`.
- **`LV_COLOR_16_SWAP` must be `0` with the Arduino_GFX driver**, not `1`
  (which is what Waveshare's own esp_lcd-based demo needs for its raw
  transmission path). Arduino_GFX already sends bytes in the order the
  panel wants; leaving the swap on renders every LVGL color wrong (theme
  blue appeared bright green) while raw `Arduino_GFX::fillScreen()`
  calls looked fine, because those bypass LVGL's color pipeline
  entirely. See `include/lv_conf.h`.
- **An SD card formatted by macOS Disk Utility can mount but fail almost
  every file read on this board's SDMMC bus** -- not a wiring, code, or
  card-quality problem. Symptom: `SD_MMC.begin()` succeeds, directory
  listing works (file/folder names all show up correctly), but opening
  actual file contents fails for the overwhelming majority of files with
  `E (...) diskio_sdmmc: sdmmc_read_blocks failed (257)`, consistently
  and reproducibly (the same tiny handful of files succeed every boot).
  Reproduced across three different SanDisk cards and multiple SDMMC
  bus configs (40MHz/20MHz, 4-bit/1-bit, with/without explicit internal
  pull-ups on CMD/D0-D3) -- none of that made any difference, ruling out
  card quality, bus speed, and pull-ups as the cause. The board's stock
  factory firmware read its own bundled SD card perfectly on the same
  physical slot, ruling out a wiring/soldering defect. The fix: erase
  and reformat the card as FAT32 with the SD Association's official
  layout (the *SD Memory Card Formatter* app, or `newfs_msdos` with an
  explicit, conservative cluster size like `-c 8`/4KB, not macOS
  Disk Utility's defaults, which produced 32KB clusters and a 4MB
  partition offset). After reformatting, `sdmmc_read_blocks failed`
  errors dropped to zero and the full library scan succeeded. Root
  cause of *why* the embedded FatFs stack chokes on Disk Utility's
  layout specifically was not conclusively identified (plausibly BPB/
  geometry fields that differ from SD-Association-compliant tools) --
  but the fix reliably works, so: **whenever "many/most files fail to
  open" shows up again, reformat the card properly before suspecting
  anything else.**
- **ESP32-audioI2S's own internal info/error logging (routed through the
  `audio_info()` weak-symbol override in `Esp32AudioI2SDriver.cpp`) is
  silent by default** even when a file plays or fails to play -- add
  `-DAUDIO_LOG -DCORE_DEBUG_LEVEL=3` to `[env:esp32-s3]`'s `build_flags`
  temporarily to get real diagnostic output (decode/stream state,
  `processLocalFile()` progress, etc.) when audio playback needs
  debugging, then remove it again once resolved -- it's not needed for
  normal operation and adds console noise.
- **ESP32-audioI2S 2.3.0 silently outputs at -6 dBFS even at volume
  21/21.** `volumetable[21]/64 == 1.0`, but `Audio::playSample()` first
  does `sample >> 1` unconditionally (EQ headroom), so half the
  PCM5100A's output voltage went unused and the stock firmware sounded
  louder. Fixed 2026-09-13 by the per-sample `audio_process_i2s` weak
  hook in `Esp32AudioI2SDriver.cpp` (x2 after `Gain()`, lossless; 21/21
  is now true 0 dBFS). Going past 0 dBFS isn't worthwhile: the PCM5100A
  is a >=1 kOhm line driver (~2.1 mA RMS), so 32 Ohm headphones (e.g.
  Marshall Major V) are current-limited anyway. **Weak-hook link
  gotcha:** a weak reference never pulls an object file out of a static
  library archive, so a hook defined in a `lib/` `.cpp` that nothing
  else references is silently dropped (the old `audio_info` diagnostic
  never actually linked). `Esp32AudioI2SDriver::begin()` lives in that
  `.cpp` to force it in; verify with `xtensa-esp32s3-elf-nm
  firmware.elf | grep audio_process_i2s`. **Earlier attempts, tried and
  reverted (2026-09-13): boosting past unity by intercepting decoded PCM
  in the library's `audio_process_extern` weak-symbol hook** (multiply
  samples, clamp, `*continueI2S = true`) -- on real hardware this made
  every track appear to finish and auto-advance within seconds with no
  audible sound at all, even though the hook's logic looks correct
  against the library's own documented convention. Root cause not
  isolated before reverting (suspect timing/reentrancy inside the
  decode loop, not the gain math itself). If revisiting a volume boost,
  don't reuse this hook without instrumenting the decode loop itself
  first (`-DAUDIO_LOG` alone did not explain it). **Also tried and
  reverted: the library's own supported 3-band EQ, `setTone(6, 6, 6)`
  (its documented max, +6dB/band)** as a safer alternative -- on real
  hardware this produced no noticeable loudness increase at all, and
  introduced a new, unrelated regression (audible playback started
  noticeably later than the elapsed-time counter, i.e. after
  `PlaybackStateMachine` had already started timing the track). Neither
  attempt is worth pursuing further; the `audio_process_i2s` fix above
  supersedes both.
- **A custom LVGL icon font generated by `lv_font_conv` renders
  completely invisibly (correct label sizing, zero pixels drawn) if this
  project's `lv_conf.h` (`LV_USE_FONT_COMPRESSED 0`) doesn't match the
  font's own compression.** `lv_font_conv` RLE-compresses glyph bitmaps
  by default; without the decompressor compiled in, glyph *metadata*
  (advance width, box size) still reads fine — a label sizes itself
  correctly — but the bitmap itself never draws, which looks identical to
  a font/encoding mismatch and can send you down the wrong debugging path
  entirely. Always pass `--no-compress` when generating a font for this
  project (see `lib/ui-widgets/IconFont.c`'s header comment, ADR 0005).
  Diagnosed via `scripts/screenshot.py` (see below) rendering a plain
  label with the font on a contrasting background, outside any button, to
  isolate font-rendering from button/theme interaction.
- **A screenshot tool exists for diagnosing on-device UI bugs without a
  phone photo or a written description**: `scripts/screenshot.sh` sends
  `SCREENSHOT\n` over serial, and `lib/ui/LvglGlue::writeScreenshotToSerial()`
  dumps a full-frame shadow buffer (kept in sync inside `flushCb`, since
  LVGL itself only ever flushes partial stripes) as raw RGB565, decoded
  into a BMP host-side. Use this whenever a UI/layout bug needs verifying
  on real hardware — much faster and more precise than asking for a photo
  or a description. Requires pyserial; the `.sh` wrapper falls back to
  PlatformIO's bundled Python if the system one lacks it.
- **An LVGL `lv_arc` reserves padding on `LV_PART_MAIN` sized for its
  (draggable) knob, even after the knob's own style is removed** — a
  ring meant to hug an edge renders visibly smaller than its host object
  unless you also `lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN)`. Found
  building the round-edge volume/unlock rings (ADR 0005,
  `lib/ui-widgets/EdgeArc.h`) via a real-hardware screenshot: the ring
  didn't reach the display's edge despite its host already being sized
  to the full framebuffer.
- **An LVGL screen (`lv_obj_create(nullptr)`) is scrollable by default,
  and a child sized larger than the screen makes that visible** as thin
  grey scrollbar lines along the screen's right/bottom edges — easy to
  mistake for a rendering artifact in the content itself. This app
  deliberately oversizes some widgets beyond the screen and lets the
  round bezel clip the excess (see `ScreenManager::renderNowPlaying()`'s
  volume ring host), so every screen now clears
  `LV_OBJ_FLAG_SCROLLABLE` in `ScreenManager::render()` — this app has
  its own swipe-gesture handling (`GestureRecognizer`) and never wants
  built-in scroll behavior anyway.
- **`ESP32-audioI2S` playback audibly stutters whenever `src/main.cpp`'s
  `loop()` is busy for stretches of time** — its `Audio::loop()` is a
  cooperative decoder that needs calling very frequently, and shared a
  single thread with LVGL's *synchronous, blocking* display flush
  (`LvglGlue::flushCb`). Any burst of frequent redraws (an animation,
  fast list scrolling) starves `loop()` of CPU time between calls and
  the I2S buffer underruns. Turning the encoder alone doesn't trigger
  this (no extra redraws), which is a useful way to tell this apart from
  other audio issues. Fixed in [ADR 0006](docs/adr/0006-audio-task-concurrency.md)
  by running `Audio::loop()` on its own FreeRTOS task pinned to core 0
  (idle otherwise — no WiFi/BT), mutex-guarded against the main thread's
  play/pause/volume calls. If audio stutter reappears, check what's
  producing heavy LVGL redraw activity, not the audio code itself first.
- **This specific board unit repeatedly goes into a state where it
  "runs" but a peripheral is silently dead, and only a real power cycle
  (unplug USB, wait, replug) fixes it -- a soft/RTS reset is not
  enough.** Seen now for the display (stayed white after a soft reset),
  the SD card (0x107 "card not responding" until reseated/repowered),
  and audio (played per every log line -- `connecttoFS=OK`,
  `processLocalFile()` with a correct `m_audioDataStart` matching the
  file's real ID3-tag size, no errors at all -- yet the jack stayed
  silent until a full power cycle). Before spending time debugging code
  for "X looks like it should work but doesn't, no errors logged" on
  this board, try a full power cycle first.
- **Boot used to take 20+ seconds before the SD library scan became
  visible; boot no longer scans the SD card at all.** Found via live
  serial capture (`millis()` timestamps around each boot step)
  2026-09-13. Two compounding causes: `computeSignature()`
  (`src/main.cpp`) does one full recursive SD directory walk
  (`SdFileLister::walk()`), ~17-19s on a real library (512 tracks, 560
  macOS AppleDouble `._` sidecar files also walked) with zero UI
  feedback; and `writeIndexCacheFile()` (`kIndexCachePath =
  "/knobify/library.idx"`) silently failed on **every single boot**
  (`vfs_api.cpp: open(): ... does not exist, no permits for creation`)
  because the SD_MMC/FATFS layer refuses to create a file inside a
  directory that doesn't exist, and nothing ever created `/knobify` --
  so the cache never persisted and every boot paid for the full walk
  *twice* (signature + `LibraryScanner::scan()`, which used to
  `lister.reset()` again) plus a full tag-read scan, forever. First fix
  (mkdir + `FileLister::rewind()` so the scanner replays an
  already-walked lister instead of re-walking) got a routine boot down
  to the one ~17-19s signature walk. Then, per a follow-up feature
  request, the signature walk was removed from boot entirely: `setup()`
  now just decodes the cached `library.idx` directly (a single small
  file read) and shows whatever that contains, instantly, even if it's
  stale. Detecting changes and rebuilding the index is now **on-demand
  only**, via Settings > "Rescan library" (`ScreenManager::runRescan()`;
  a refresh button on the library root until ADR 0010) that calls a small `library::LibraryRescanner`
  interface (`lib/library/LibraryRescanner.h`) implemented in
  `src/main.cpp` (`SdLibraryRescanner`) so the UI layer doesn't need to
  know about the concrete SD types. A device with no cache yet (e.g.
  first boot after flashing) just shows an empty library until the user
  runs it.
- **A UI action that runs a synchronous, multi-second blocking call
  (like the scan button above) must NOT call `lv_timer_handler()` to
  flush progress to the screen while it's running -- it corrupts touch
  input app-wide, not just on that screen.** `LvglGlue::pump()` (called
  every `loop()`) *is* `lv_timer_handler()`; a button's `LV_EVENT_CLICKED`
  handler runs from inside that very call, so calling
  `lv_timer_handler()` again from inside the handler is a reentrant call
  into LVGL's own timer/input dispatch -- LVGL explicitly does not
  support this. Symptom on real hardware (2026-09-13): after tapping the
  scan button once, taps became unreliable everywhere in the app (list
  items, the lock button, playback controls), not just around the scan
  UI -- easy to misdiagnose as "the button is dead" or "SD scan runs in
  the background" (neither was true; the scan was synchronous and
  finished, but input stayed corrupted afterward). Fix: use
  `lv_refr_now(nullptr)` instead, which only forces the pending redraw
  without touching input devices or other timers, so it's safe to call
  from inside an event handler. Also delete any scratch UI created for
  the duration (e.g. a progress overlay) with `lv_obj_del_async()`, not
  `lv_obj_del()`, for the same reason `ScreenManager::render()` already
  does for screen swaps triggered from a click handler (see that
  function's own comment). See
  `lib/ui/ScreenManager.cpp`'s `ScanProgressLabelListener` and
  `onScanClicked()`.
- **A full recursive SD directory walk (`computeSignature()` /
  `SdFileLister::reset()`), immediately followed by a batch of
  individual file opens in the same call, reliably drives the SD_MMC
  peripheral into `sdmmc_read_blocks failed (257)` for every single one
  of those opens** -- found 2026-09-13 while adding album-cover
  generation that ran right after this walk. Confusingly, this is NOT a
  generic "SD card is flaky" issue: an isolated single file open done
  elsewhere (e.g. starting playback) succeeds reliably even immediately
  afterward, and a genuine full library rescan (which does the exact
  same "walk then open every file" pattern) has worked before. It also
  survives a full chip reset (even `esp_deep_sleep_start()`, which
  resets far more hardware state than a normal reboot) with 100%
  reproducibility, which points at the SD *card's own* internal
  controller being left in a bad state (not an ESP32-side register) --
  and if a battery is connected (PH1.25 connector), unplugging USB does
  **not** actually power-cycle the board, so the usual "real power
  cycle" fix for a stuck peripheral may not even be exercisable. Found
  this leaves `SdFileLister::reset()`'s own top-level directory handle
  unclosed too (only `walk()`'s child entries were being closed) --
  fixed, but closing that leak alone did NOT fix the read failures, so
  don't assume it's the whole story if this resurfaces. Workaround
  adopted: don't batch-generate covers right after a walk at all --
  `ScreenManager::renderNowPlaying()` instead generates a missing cover
  lazily, from the one isolated file open playback already needs, the
  first time an album is actually played. If a future feature needs to
  do many individual file opens right after a directory walk again,
  expect this same failure and budget time to design around it (e.g.
  interleave the opens into the walk itself) rather than pacing/delays,
  which did not help in testing.
- **Most real-world embedded cover art is Progressive JPEG** (~90% of
  this library's ID3 APIC and MP4 covr pictures), which TJpg_Decoder
  couldn't decode at all. Since ADR 0016 covers go through JPEGDEC
  (`lib/drivers-jpeg/JpegDecAdapter.h`): progressive images decode from
  their DC coefficients at 1/8 scale (19 ms, ~11 KB for 600 px). Don't
  switch to a full progressive decoder: stb_image needed 4.2 MB of PSRAM
  at 600 px and ran out above ~1000 px (measured 2026-09-15).
- **The firmware uses TinyUSB, not the S3's USB-Serial-JTAG** (ADR 0016,
  `ARDUINO_USB_MODE=0`), for the USB drive. Consequences found on the
  device 2026-09-16:
  - Flashing needs a **1200-baud touch** to reboot into the ROM
    bootloader (esptool's DTR/RTS reset fails with "No serial data
    received"); `scripts/flash-primary-mcu.sh` does it. The first switch
    from a USB-Serial-JTAG build needed one USB replug.
  - **A crash's output never reaches USB** (the port reappears after
    boot). Send `INFO` over serial: reset reason 4 is a panic. Then run
    `scripts/read-coredump.sh` (the core dump sits in the `coredump`
    partition) with the ELF of the crashing build.
  - **`Serial.write()` spins forever while a host holds the port open
    without reading** (Arduino-ESP32 2.0.x `USBCDC::write` has no
    timeout) -- the whole loop freezes. Keep a monitor reading
    continuously, and never run two readers on the port.
  - The port drops on every USB drive start/stop (re-enumeration):
    `UsbMscStorage::printEvents()` prints the drive's host events later.
  - Serial commands for driving the device without a hand on it:
    `TAP x y`, `KNOB n`, `INFO`, `SCREENSHOT`.
  - Bulk transfer over the old USB-Serial-JTAG CDC dropped bytes; TinyUSB
    CDC with an 8 KB ack per chunk was reliable but slow (0.14 MB/s) and
    stalled once after ~30 MB. Use USB drive mode for files.
- **ESP32-audioI2S 2.3.0 calls the weak `audio_info()` hook without a null
  check when it prints AAC codec parameters** (`Audio::showCodecParams()`)
  -- without a definition the first decoded M4A frame jumps to address 0
  and panics (`InstFetchProhibited`, pc 0, task `audio`). MP3 never
  reaches that call. `Esp32AudioI2SDriver.cpp` defines the hook; keep it.
- **USB drive on macOS**: macOS reads the entire FAT when mounting, at the
  drive's ~0.87 MB/s. 4 KB clusters on a 32 GB card (31 MB FAT) time out;
  32 KB clusters (3.9 MB) mount in ~6 s. And **a locked Mac ejects new
  removable storage right after probing it** (START STOP UNIT with eject,
  ~1.5 s after export) -- check `CGSSessionScreenIsLocked` before
  debugging the firmware.
- **UI colors must be judged on the device, not a monitor or a
  screenshot's hex values.** The panel is RGB565 (subtle neutrals get
  rounded — `#F4F4F0` arrived as neutral `#F6F6F6`) and visibly shifts
  toward green. Attempts to "warm" the surface to compensate all looked
  off on the device, so it's the original Snow White `#F4F4F0` — don't
  re-tint it without checking on hardware. Use `lib/ui/Theme.h` tokens
  only. See ADR 0008.
- **`LV_LABEL_LONG_DOT` does nothing on a content-height label** — it
  just wraps. Use `setClampedText()` in `ScreenManager.cpp` (explicit
  line budget). LVGL's built-in Montserrat fonts are also ASCII-only:
  umlauts, accents and `·` render as boxes. See ADR 0008.
- **Small heap allocations must go to PSRAM, or SD reads fail.** ESP-IDF
  keeps every `malloc` under 4096 bytes in internal RAM by default, so
  the library index's and a play queue's path strings filled it (38.9 KB
  free after boot, 1.7 KB after queueing 512 tracks) and the SD driver's
  DMA buffers then failed: `sdmmc_read_blocks failed (257)`
  (`ESP_ERR_NO_MEM`), `connecttoFS=FAILED`, a library shuffle showed a
  track but never played. `setup()` now calls
  `heap_caps_malloc_extmem_enable(32)` first (93.9 KB internal free after
  boot, unchanged by the queue). If SD opens start failing with 257,
  check internal heap before suspecting the card. See ADR 0011.
- **ESP32-audioI2S's positions and durations are estimates, and wrong
  for this library.** Every MP3 here is VBR (checked 2026-09-15). The
  library ignores the Xing/VBRI header and derives duration and byte/time
  conversions from the average bitrate of the first ~200 frames, so the
  shown end time started minutes too long and shrank over the first
  seconds of each track. `library::Mp3Duration` reads the exact frame count
  instead; `Esp32AudioI2SDriver` uses it for `durationSeconds()` and seek
  sizes. Also: `getFilePos()` is the *reader*, a whole input buffer ahead
  of what is heard -- subtract `inBufferFilled()` (resume, seeks). And a
  seek (`setFilePos`) makes the library discard its input buffer and stay
  silent until it is full again, so the buffer size is audible: the 300 KB
  default gave ~290 ms of silence per seek, `kInputBufferBytes` (64 KB)
  ~80 ms (measured with a sample-gap log in `audio_process_i2s`). See ADR
  0012 and ADR 0013.

## Where things are documented (so you add to the right place)

- **Pure hardware facts** (pinout, board identification, electrical
  quirks) → `device.md`.
- **Architecture-level decisions and why** (navigation model, why a
  library was swapped, format decisions) → an ADR in `docs/adr/` (see
  `docs/coding-guidelines.md`'s Documentation section for when one's
  warranted).
- **A bug found and fixed during hardware bring-up** → the
  "Implementation status" narrative in the relevant ADR (currently ADR
  0004), in the order found, with root cause and fix -- not just the
  end state. That history is what saves the next debugging session.
- **A fact any agent should see before starting work at all** → this
  file, as a short pointer into the above -- not a replacement for them.
