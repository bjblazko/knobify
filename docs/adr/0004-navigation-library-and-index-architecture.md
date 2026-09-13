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

See [`docs/design/ux-guidelines.md`](../design/ux-guidelines.md) for the
full design-philosophy treatment of these constraints and the interaction
principles they drive.

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
Album → Track) and **Files** (raw folder browse, arbitrary depth), modeled
as two swipeable top-level tabs rather than a picker screen. A
`TabController` owns one `NavigationStack` per tab (roots: `Screen{Artists}`
and `Screen{Folder, path="/"}`) and the currently active tab. A left-right
swipe pops the active stack if `canGoBack()` is true; otherwise (nothing
to go back to — you're at a tab root) the same gesture switches tabs. See
[`ux-guidelines.md` §5](../design/ux-guidelines.md#5-navigation-model--gesture-flows)
for the rationale behind reusing one gesture this way.

### Encoder: context-sensitive by current screen

On browse screens (Artists/Albums/Tracks/Folder), rotating the encoder
moves the highlighted/scrolled list item. On the Now Playing screen,
rotating adjusts volume. `InputRouter` reads the active screen's kind
from `TabController`/`NavigationStack` to decide which it means. See
[`ux-guidelines.md` §5](../design/ux-guidelines.md#5-navigation-model--gesture-flows)
for why this mapping has to be context-sensitive rather than mode-based.

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
shown. See
[`ux-guidelines.md` §5](../design/ux-guidelines.md#5-navigation-model--gesture-flows)
for the discoverability rationale.

### Animation: screen transitions only, for v1

Screen push/pop/tab-switch transitions slide via LVGL's built-in
`lv_scr_load_anim`, matching the gesture's direction. See
[`ux-guidelines.md` §5](../design/ux-guidelines.md#5-navigation-model--gesture-flows)
for why animation is scoped this narrowly for v1.

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

**Display/touch/UI are implemented and confirmed working on the physical
board** (2026-09-11/12). The ST77916 QSPI init sequence and CST816 I2C
touch protocol were ported verbatim from Waveshare's own official demo
for this board (via the
[Sandjab/Waveshare-Knob](https://github.com/Sandjab/Waveshare-Knob) demo
mirror), not reconstructed from a summary — see
`lib/drivers-display/St77916InitOps.h` and
`lib/drivers-touch/Cst816Driver.h`. Two real bugs surfaced only by
flashing real hardware, neither visible from compilation alone:

- **ESP-IDF's `esp_lcd_panel_io_spi` never actually drove the panel.**
  The first display implementation vendored Waveshare's `esp_lcd_sh8601`
  driver against ESP-IDF's `esp_lcd` component (adapting several API
  differences between the newer IDF that demo targets and the older
  4.4.x bundled with this project's PlatformIO/Arduino core — color
  space field rename, `disp_off` field rename, no `quad_mode` flag on
  `esp_lcd_panel_io_spi_config_t`). It compiled and every call returned
  `ESP_OK`, but nothing ever reached the panel — QSPI (4-line)
  transaction support in that API was added in a later ESP-IDF than this
  platform bundles. **Fix**: replaced it with
  [Arduino_GFX](https://github.com/moononournation/Arduino_GFX)'s
  `Arduino_ESP32QSPI` bus class, which implements QSPI itself directly
  against `spi_master` rather than going through `esp_lcd_panel_io_spi`.
  Pinned to v1.4.9 specifically (see `platformio.ini`): newer releases
  require `esp32-hal-periman.h`, an Arduino-ESP32 3.x/ESP-IDF 5.x header
  this project's core doesn't have. That version's own `Arduino_ST77916`
  class doesn't accept a custom init table (added later), so
  `lib/drivers-display/KnobSt77916.h` is a small vendored equivalent
  built against that version's still-stable `Arduino_TFT`/
  `Arduino_DataBus` base classes, using this project's own verified init
  sequence.
- **Missing COLMOD command → washed-out grey instead of color.** The
  original `esp_lcd_sh8601.c` driver always sends `COLMOD=0x55` (16bpp
  RGB565) and `MADCTL` automatically before applying the vendor-specific
  command table — logic that didn't carry over when porting to
  `KnobSt77916`. Without it the panel stayed in its power-on default
  pixel format; a solid-color test rendered as uniform grey/white tones
  instead of distinct colors, which looked enough like a partial-fill
  addressing bug (worth noting: initial hardware photos were taken at an
  angle, making horizontal test bands look diagonal and briefly
  suggesting a QSPI stride bug that didn't exist) to cost real
  debugging time before the missing command was found. Fixed by adding
  `COLMOD`/`MADCTL` explicitly at the start of `St77916InitOps.h`.

`lib/ui/ScreenManager` implements all five screen kinds (Artists, Albums,
Tracks, Folder, NowPlaying) plus the mini-bar in one file rather than
split per screen, deliberately, since the layout was still being
validated on real hardware when this was written (round-display safe
areas, touch target sizes) -- splitting further before that settles
would be premature. The gesture-hint nudge animation and
screen-transition animation (decisions 11-12) are NOT yet implemented;
screens currently hard-cut. `lv_conf.h` is copied from the installed
LVGL package's template with `LV_COLOR_16_SWAP` and `LV_TICK_CUSTOM`
(via Arduino `millis()`) enabled to match Waveshare's config.

**UI is functional but not yet well-designed.** The first on-device look
used no explicit LVGL theme (falling back to LVGL's own dated default)
combined with hand-rolled highlight colors that didn't set text color,
making unfocused list rows' text invisible (dark-on-dark). Fixed by
calling `lv_theme_default_init()` with a deliberate dark/blue palette in
`LvglGlue::begin()`, and switching `ScreenManager::applyHighlight()` from
manual `bg_color` overrides to toggling `LV_STATE_CHECKED` (with list
buttons marked `LV_OBJ_FLAG_CHECKABLE`), so the theme's own
contrast-correct checked/unchecked styling applies instead of ad hoc
colors. User feedback after that fix: "better" but not yet fully
resolved — visual design of the screens (typography, spacing, motion)
is real follow-up work, not a solved problem.

**Further real bugs found through iterative on-device use (2026-09-12)**,
each one only visible by actually tapping through the UI on hardware:

- **LVGL's own colors rendered wrong (theme blue appeared bright
  green).** `LV_COLOR_16_SWAP` was left at `1`, copied from Waveshare's
  original esp_lcd-based demo (which needed it for that raw SPI
  transmission path). Arduino_GFX's `writePixels()`/`write16()` already
  send bytes in the order this panel expects, so the swap was
  double-handling byte order for every LVGL-drawn color. The earlier
  `fillScreen`/`fillRect` hardware diagnostics never caught this because
  they called Arduino_GFX directly, bypassing LVGL's color pipeline
  entirely — a real gap in that testing. Fixed: `LV_COLOR_16_SWAP 0` in
  `include/lv_conf.h`.
- **Tapping a list item briefly navigated forward, then immediately
  reverted.** `ScreenManager::render()` called `lv_obj_del()` on the old
  screen synchronously from inside the very click-event handler that
  triggered the navigation — deleting a widget (and its screen) while
  LVGL is still processing that widget's event corrupts LVGL's input
  state. Fixed with `lv_obj_del_async()`, which defers the deletion until
  after event processing completes.
- **A single tap could also register as a swipe-back.** Touch was polled
  twice per `loop()` iteration — once inside LVGL's own indev callback,
  once separately for `GestureRecognizer`'s swipe detection — each doing
  its own fresh I2C read. Real capacitive touch coordinates jitter
  slightly between consecutive reads, so the two consumers could see
  different enough coordinates for the same physical tap that one read
  it as a plain click and the other read it as a >=40px swipe. Fixed by
  polling once per loop in `src/main.cpp` and feeding that single sample
  to both `LvglGlue::feedTouch()` and `GestureRecognizer` — LVGL's indev
  no longer polls the driver itself.
- **The supplementary back button (added after user feedback that swipe
  alone wasn't discoverable) was invisible/unclickable on every list
  screen, though it worked fine on Now Playing.** It was being created
  *before* the full-screen list widget, which drew on top of it and
  intercepted its taps; Now Playing has no full-screen widget covering
  that area, so it stayed visible there. Fixed by creating the back
  button last in `ScreenManager::render()`, regardless of which screen
  kind is being rendered, so it's always the topmost/frontmost child.
- **List rows' top edge, and the round-safe back-button area, were
  clipped or crowded.** Added `pad_top` on the list widget and confirmed
  the back button's top-center position (12px inset) clears the round
  bezel cleanly.
- **The rotary encoder didn't respond to volume changes at all**, after
  also going through two failed attempts at fixing an earlier jitter/
  reversed-direction report (a divide-by-fixed-constant debounce, then a
  fixed-time debounce, then a textbook full-step Gray-code state
  machine) that produced progressively worse or still-jittery results.
  The actual root cause, found by logging every raw pin state the ISR
  observed during a real turn (see the temporary `drainLog()` diagnostic
  added and removed in this pass) rather than continuing to guess: this
  encoder's raw states *never include `00`* (both contacts closed) --
  only `11` (rest), `01`, and `10`. Every decoder tried up to that point
  assumed the standard 4-state Gray-code cycle and keyed "count a step"
  off passing through `00`, so none of them could ever fire on this
  hardware. Replaced with a decoder matching the real 3-state behavior
  (track which single contact closed since the last rest state, count on
  release back to rest) plus a short 3ms refractory window after each
  counted step to reject overshoot between fast consecutive detents. See
  `lib/drivers-encoder/GpioEncoderDriver.h` and the hardware-gotchas
  index in `AGENTS.md`. Lesson generalized there: when an input decoder
  doesn't behave as expected, capture the actual raw hardware signal
  before writing another decoder attempt against an assumed model.

- **"Only ~2 songs found, no audio, counter frozen" traced to SD card
  formatting, not code (2026-09-12/13).** Real hardware showed the
  library scan finding only a handful of tracks across three different
  SanDisk cards. First fix: `isAppleDoubleSidecar()` in
  `SdFileLister.h` compared macOS's `._name` sidecar-file prefix against
  the start of `entry.name()`'s return value, but this project's
  SD_MMC/File stack returns the *full path* from `.name()` (not just the
  filename) -- so the check never matched, and `._`-prefixed sidecar
  files (which pass the audio-extension filter) kept being scanned as
  broken tracks. Fixed by checking the substring after the last `/`; the
  identical bug was proactively also fixed in `SdDirectoryReader.h`
  (Files-mode browsing), which had the same full-path-vs-basename
  problem for a different reason (storing the raw name into
  `FolderEntry`). But after that fix, diagnostic logging added to
  `LibraryScanner`/`main.cpp` (a `ScanProgressListener::onFileResult`
  override logging every file's open/tag-parse outcome) showed the
  *real*, dominant cause: the overwhelming majority of genuine,
  non-sidecar audio files were also failing to open, with
  `E (...) diskio_sdmmc: sdmmc_read_blocks failed (257)` --
  a block-level timeout from the SDMMC host controller itself, below
  any filesystem/code logic. This reproduced identically across three
  different high-quality SanDisk cards and every SDMMC bus
  configuration tried (40MHz/20MHz, 4-bit/1-bit width, with/without
  explicit internal pull-ups on CMD/D0-D3) -- ruling out card quality,
  bus speed, and missing pull-ups. The board's stock factory firmware
  read its own bundled SD card on the same physical slot with zero
  errors, ruling out a wiring/soldering defect. The actual cause: the
  affected card had been formatted by macOS Disk Utility (32KB
  clusters, 4MB partition offset) rather than to the SD Association's
  own FAT32 layout spec; reformatting with `newfs_msdos -F 32 -c 8`
  (or the official SD Memory Card Formatter tool) made
  `sdmmc_read_blocks failed` errors disappear entirely and the full
  library scan succeed. See the hardware-gotchas index in `AGENTS.md`
  for the short version -- read that first the next time "most files
  fail to open" shows up, before re-suspecting code, cards, or wiring.
- **Small buttons "missed" taps; list rows didn't (2026-09-13).** Lock,
  back and scan almost never fired, prev/play often missed, next and
  list rows worked. Suspected undersized hit areas or touch timing, but
  serial logging of every poll edge, every LVGL indev read and every
  PRESSED/CLICKED event showed each tap *was* delivered -- just at the
  wrong X. A crosshair calibration (targets at x=90/180/270) gave raw
  X ~= 1.18 * visual - 66 (Y correct): taps landed 20-50px left of the
  finger, outside any button narrower than that, while full-width rows
  and the right-hand next button still caught them. Fix:
  `lib/input/TouchCalibration.h` applied right after the poll. The same
  capture showed `loop()` taking ~65ms during Now Playing redraws, so a
  quick tap could fall between two LVGL reads -- `lib/input/TouchLatch.h`
  now guarantees every polled press reaches LVGL. Buttons also got a
  10px extended click area (`LvglButtonHelpers.h`). Note: the CST816
  NACKs every I2C read while untouched, so a failed read genuinely means
  "not pressed" -- don't "fix" it by holding the previous state. The
  diagnostics stay in the code behind `-DKNOBIFY_TOUCH_DEBUG` (plus a
  `CALIB` serial command that draws the crosshairs).

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
