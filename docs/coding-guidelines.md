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

## File and module organization

- Organize code by **domain**, not by technical layer — a `lib/playback/`
  module owns everything about playback (state machine, its driver
  interface, its native tests conceptually belong together), rather than
  scattering "all state machines" and "all drivers" into layer-wide
  buckets.
- Keep files small and single-purpose. If a file is doing more than one
  job, or you have to scroll to remember what's at the top, split it. As
  a rough guide, reach for a split well before a file passes a few
  hundred lines — the number matters less than whether the file still has
  one clear reason to change.
- Apply single-responsibility at the class/function level too: a class
  should have one reason to change. A constructor that also parses files,
  or a playback function that also touches the display, is a sign the
  responsibilities need separating.
- Prefer several small, well-named files over one large file with
  sections — it's easier to navigate, easier to test in isolation, and
  keeps hardware/logic separation (above) honest, since a mixed-concern
  file is exactly where hardware calls tend to leak into logic code.

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
- Don't be shy about Mermaid diagrams in Markdown docs (arc42 especially)
  — a component diagram, sequence diagram, or state chart is often
  clearer than a paragraph of prose for building-block/runtime views.
  Reach for one whenever a picture would answer the reader's question
  faster than text.
