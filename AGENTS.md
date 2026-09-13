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
