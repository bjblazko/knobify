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
  visual - 66), corrected by `lib/input/TouchCalibration.h`. "Buttons
  miss taps" → build with `-DKNOBIFY_TOUCH_DEBUG`, send `CALIB` over
  serial and re-fit before touching sizes/timing. See ADR 0004.

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
- **ESP32-audioI2S's `setVolume()` caps at 21/21 = true unity gain
  (0dB)** -- it never digitally amplifies above the source signal's own
  level, by design (`volumetable[21]/64 == 1.0` in the library's
  `Audio.cpp`). This board's stock factory firmware can go noticeably
  louder at max volume, so 21/21 being quieter than the demo is expected
  library behavior, not a bug in this app's volume code. **Tried and
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
  attempt is worth pursuing further without a much better understanding
  of this library's gain/EQ/timing internals than a quick real-hardware
  trial gives you -- 21/21 (true unity gain) is the accepted ceiling for
  now.
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
  only**, via a refresh-icon button (`LV_SYMBOL_REFRESH`) on the library
  screen's root (`ScreenManager::renderScanButtonIfNeeded()`,
  `onScanClicked()`) that calls a small `library::LibraryRescanner`
  interface (`lib/library/LibraryRescanner.h`) implemented in
  `src/main.cpp` (`SdLibraryRescanner`) so the UI layer doesn't need to
  know about the concrete SD types. A device with no cache yet (e.g.
  first boot after flashing) just shows an empty library until the user
  taps that button.
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
- **Most real-world embedded ID3 cover art (APIC frames) is Progressive
  JPEG, which `TJpg_Decoder` (this project's on-device JPEG library,
  `lib/drivers-jpeg/TJpgDecoderAdapter.h`) cannot decode at all** --
  `TJpgDec.getJpgSize()` simply fails. Found 2026-09-13: sampling this
  library's covers showed ~90% (194/216) were progressive. Baseline
  JPEGs decode and render correctly (verified end-to-end on real
  hardware). Fix is upstream of the device: `scripts/convert-music-library.sh`
  now forces embedded art to baseline JPEG via ffmpeg's `mjpeg` encoder
  (`-codec:v mjpeg` instead of `copy`) for both the mp3-passthrough and
  transcode paths, while leaving the audio itself untouched
  (`-codec:a copy`/unchanged bitrate settings) -- re-run that script
  (into a fresh or emptied destination; it skips files that already
  exist) to pick up correctly-decodable covers. `JPEGDEC`
  (bitbank2/JPEGDEC) was considered as an alternative that natively
  supports progressive JPEG, but it only does "thumbnail (DC-only)"
  decoding of progressive images, not a full decode -- fixing the
  source data was judged better than accepting that quality/complexity
  trade-off.
- **This board's native USB-CDC serial silently drops bytes under
  sustained load with no error and no flow control** -- found
  2026-09-13 sending a JPEG file to the device over a throwaway
  diagnostic serial command: a single `Serial.write()` of a few hundred
  bytes from the host reliably arrived truncated (the device-side loop
  just stops receiving further bytes, forever, with no crash or
  timeout). Sending in small chunks (~32-512 bytes) with a short
  `time.sleep()` and explicit `flush()` between chunks made small
  payloads (a few hundred bytes) reliable, but a real ~80KB file still
  stalled partway even chunked -- plain serial is not viable for
  bulk/file-sized transfers on this board at all; use WiFi (or physical
  SD card access) instead for anything larger than a few hundred bytes.

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
