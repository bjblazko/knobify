# 0004: Navigation, library indexing, and index cache architecture

## Status

Accepted — 2026-09-11

## Context

v1 needs a concrete UX (see [ADR 0002](0002-v1-format-and-mcu-scope.md) for
scope: browse by artist/album, touch + one rotary encoder, fully offline).
Two hardware facts constrain the design, confirmed this session since
neither is documented anywhere for this board:

- The primary MCU's rotary encoder is **rotation-only** (A/B on GPIO7/
  GPIO8 per community sources) — there is no click/push button. With no
  button to switch modes, what a knob turn *means* must be decided by
  context (which screen is showing), not by an explicit mode toggle.
- The display is a **physically round** 1.8" panel behind a 360×360
  square framebuffer — the corners are clipped by the bezel, which rules
  out corner-anchored UI affordances like a fixed back button.

The user also wants the ability to browse the SD card as a raw folder
tree (not just by tag-derived Artist/Album), gestures to be discoverable
without a persistent UI element or a separate tutorial screen, and a
navigation architecture that doesn't hardcode today's boot screen as the
permanent app root, since a future home/menu screen (web radio, podcasts,
settings) is expected later.

## Decision

### Navigation: an injectable-root screen stack, wrapped by two tabs

`NavigationStack` is a fixed-capacity stack of `Screen{kind, params}`
values, constructed with an **injected root screen**. It never special-
cases any particular `ScreenKind` as "the" root — `main.cpp` decides
today's root is `Screen{Artists}`; inserting a future `Home` screen above
it is a one-line change at the call site, not a rework of the stack.

Two peer browsing modes exist as of v1: **Library** (tag-based Artist →
Album → Track) and **Files** (raw folder browse, arbitrary depth). Rather
than a picker screen, they're modeled as two swipeable top-level tabs. A
`TabController` owns one `NavigationStack` per tab (roots: `Screen{Artists}`
and `Screen{Folder, path="/"}`) and the currently active tab. A left-right
swipe pops the active stack if `canGoBack()` is true; otherwise (nothing
to go back to — you're at a tab root) the same gesture switches tabs. This
reuses one gesture for two purposes based on context, matching the same
context-sensitivity principle applied to the encoder below, and needs no
extra screen or picker UI.

### Encoder: context-sensitive by current screen

On browse screens (Artists/Albums/Tracks/Folder), rotating the encoder
moves the highlighted/scrolled list item. On the Now Playing screen,
rotating adjusts volume. `InputRouter` reads the active screen's kind
from `TabController`/`NavigationStack` to decide which it means — no mode
button exists, so the mapping must be unambiguous per screen.

### Library indexing: tags are the source of truth, folders are a separate live view

For **Library** mode, ID3v2 (MP3), Vorbis comments (OGG), and RIFF INFO
(WAV) tags — not folder layout — determine Artist/Album/Track grouping;
untagged files fall back to "Unknown Artist"/"Unknown Album" and
filename-as-title. Tag parsers are hand-rolled (`Id3v2Parser`,
`VorbisCommentParser`, `RiffInfoParser`) against a minimal `RawFile`
interface, rather than reusing `ESP32-audioI2S`'s `audio_id3data()`
callback or `arduino-audio-tools`'s `MetaDataOutput`: both extract tags
only as a side effect of streaming a file through a decoder, which is
impractical to invoke per-file across a full-library scan of hundreds of
files, and isn't host-testable without a fake `Stream`. The hand-rolled
parsers are small (~100-150 lines each), bounded in scope, and testable
against hand-built byte buffers on the host.

**Files** mode does not use the tag-based index at all — `FolderBrowser`
lists a directory's immediate entries on demand via the same `FileLister`
interface used for the library scan, with no tag parsing and no caching,
so it always reflects the SD card's actual current contents. This is
deliberately the fallback/raw view for when tags are wrong, missing, or
the user just wants to see what's on the card.

### Index cache: persisted on SD, cheap staleness check on boot

The Library-mode index is built once by a full tag-parsing scan, then
persisted to `/knobify/library.idx` (magic, format version, a staleness
signature, flat artist/album/track tables). On subsequent boots, a
cheap stat-only pass (file count + XOR of `size ^ mtime` across all
audio files) is compared against the cached signature; a match loads the
cache directly, a mismatch (or a missing/corrupt cache file) triggers a
full rescan. This avoids re-parsing tags for every file on every boot
while keeping the check itself proportional to a directory walk, not a
full read.

### Gesture discoverability: one-time animated nudge, not a persistent icon

Each gesture (swipe-to-back, swipe-to-switch-tab) is taught by briefly
animating the screen content in the gesture's direction and settling back,
shown once **ever** per gesture type — tracked via a persisted flag in
NVS, not per-boot — the first time a screen where that gesture applies is
shown. This teaches discoverability without adding a permanent UI element
or a separate onboarding flow, and doesn't repeat on every power cycle.

### Animation: screen transitions only, for v1

Screen push/pop/tab-switch transitions slide via LVGL's built-in
`lv_scr_load_anim`, matching the gesture's direction. This is the one
animation investment scoped for v1: it reuses a single mechanism (the
navigation stack transition) to make the whole app feel polished, rather
than building several one-off effects. Further animation ideas (e.g. Now
Playing flourishes) are deferred.

## Implementation status

Navigation (`NavigationStack`/`TabController`), library indexing (tag
parsers, `LibraryScanner`, `FolderBrowser`, `IndexCache`), playback
(`PlaybackStateMachine`, debounced `VolumePersistence`), and input
routing (`GestureRecognizer`, `InputRouter`) are implemented and
host-tested (`scripts/check.sh`). Hardware drivers for SD (`SD_MMC`,
4-wire), the rotary encoder (interrupt-driven quadrature on GPIO7/8),
NVS (`Preferences`), and audio (`ESP32-audioI2S`, pinned v2.3.0) are
wired up in `src/main.cpp`, using the pinout in `device.md` (sourced
from a community reference for this exact board, not yet independently
verified against the physical hardware).

**Display/touch/UI are now implemented too.** The ST77916 QSPI init
sequence and CST816 I2C touch protocol were ported verbatim from
Waveshare's own official demo for this board (via the
[Sandjab/Waveshare-Knob](https://github.com/Sandjab/Waveshare-Knob) demo
mirror), not reconstructed from a summary — see
`lib/drivers-display/Sh8601InitCmds.h` and `lib/drivers-touch/Cst816Driver.h`.
The vendored `esp_lcd_sh8601.c`/`.h` panel driver needed adapting in
several places for API drift between the ESP-IDF version that demo
targets and the older one (4.4.x) bundled with this project's
PlatformIO/Arduino core — see comments at each adapted spot (color space
field rename, `disp_off` field rename with inverted boolean, no
`quad_mode` flag on `esp_lcd_panel_io_spi_config_t` in this IDF version).
None of this has been verified on the physical board yet — compilation
succeeding is not the same as the display actually working; the
`quad_mode` omission in particular is flagged as unverified until tested.

`lib/ui/ScreenManager` implements all five screen kinds (Artists, Albums,
Tracks, Folder, NowPlaying) plus the mini-bar in one file rather than
split per screen, deliberately, since the layout hasn't been validated on
real hardware yet (round-display safe areas, touch target sizes) --
splitting further before that happens would be premature. The gesture-hint
nudge animation and screen-transition animation (decisions 11-12) are
NOT yet implemented; screens currently hard-cut. `lv_conf.h` is copied
from the installed LVGL package's template with `LV_COLOR_16_SWAP` and
`LV_TICK_CUSTOM` (via Arduino `millis()`) enabled to match Waveshare's
config.

## Consequences

- The navigation/tab/gesture logic (`NavigationStack`, `TabController`,
  `GestureRecognizer`, `InputRouter`) is entirely hardware-decoupled and
  host-testable, per [ADR 0003](0003-testing-strategy.md); only the LVGL
  screen rendering and transition animation are hardware-facing.
- Two independent scanning paths exist over the SD card (Library's
  cached tag-based scan vs. Files' live uncached listing) — some
  duplicated file-walking logic is expected, but they share the
  `FileLister` interface, so hardware access itself isn't duplicated.
- If the rotation-only assumption about the encoder turns out to be
  wrong (e.g. a click pin is found during wiring), the context-sensitive
  encoder mapping and the swipe-based tab switch should be revisited —
  they were specifically designed around the absence of a button.
- Adding a future home/menu screen only requires changing which
  `Screen` each `NavigationStack` is constructed with, not restructuring
  `NavigationStack` or `TabController` themselves.
