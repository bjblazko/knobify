# Coding Guidelines

## Hardware/logic separation (the most important rule)

Any code that directly calls a hardware API (Arduino `Wire`/`SPI`/`I2S`
calls, LVGL display flush callbacks, touch controller reads, SD card I/O,
the `ESP32-audioI2S` library) lives in a driver/adapter module and is kept
as thin as possible — just enough to translate between the hardware API and
a plain C++ interface.

Everything else (playback state, library/metadata parsing, playlist logic,
UI state machines) is written against those interfaces, never against the
concrete hardware calls directly. This is what makes
[host-native unit testing](adr/0003-testing-strategy.md) possible at all —
if hardware calls leak into logic code, that logic becomes untestable
without a board.

Rule of thumb: if a function's unit test would need a real ESP32-S3 to
pass, it's in the wrong layer.

## Project layout

- `src/` — application entry point and wiring (constructs concrete drivers,
  injects them into logic classes).
- `lib/` — internal libraries, one directory per module, each with a clear
  single responsibility (e.g. `lib/playback/`, `lib/library/`,
  `lib/drivers/display/`).
- `include/` — shared headers, if any live outside `lib/`.
- `test/test_native/` — host-native unit tests (Unity), one test file per
  module under test, no hardware dependencies.

## Style

- Format with `clang-format` (config to be added as `.clang-format` once
  the first real source files exist — don't invent a style in the abstract
  before there's code to format).
- Naming: `PascalCase` for types/classes, `camelCase` for functions and
  variables, `SCREAMING_SNAKE_CASE` for compile-time constants and macros,
  matching common C++/Arduino convention.
- Prefer explicit types over `auto` where the type isn't obvious from the
  right-hand side.
- No dynamic allocation in hot paths (audio callback, display flush) —
  standard embedded practice given fixed RAM; allocate buffers once at
  startup.

## Commits

- Commit messages describe *why*, not just *what* — especially for
  anything touching an ADR-level decision (reference the ADR number if
  applicable).
- Keep commits scoped to one logical change.

## Documentation

- A consequential decision (language/library choice, format support,
  architecture shift) gets an ADR in `docs/adr/` before or alongside the
  code that implements it — not written up after the fact from memory.
- Update `docs/arc42/` sections as the pieces they describe actually get
  built, not speculatively ahead of the code.
