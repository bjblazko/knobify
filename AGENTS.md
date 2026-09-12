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
