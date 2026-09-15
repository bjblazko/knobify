# 0005: Device lock, display power, and round-edge UI widgets

## Status

Accepted — 2026-09-13

## Context

knobify is used in two real contexts: sitting on a table (where a stray
touch barely matters, but the display burning power for no reason does)
and riding in a pocket while playing (where accidental touch/encoder
input against fabric is a real problem). This extends
[ADR 0004](0004-navigation-library-and-index-architecture.md)'s
input/navigation model with device lock and display power, and brings
round-edge progress indicators into scope for volume and unlock feedback
— all three were previously listed as out of scope in the README. See
[`ux-guidelines.md` §6](../design/ux-guidelines.md#6-power-lock--status-flows)
for the full design rationale behind these two usage contexts.

The same hardware facts from ADR 0004 still constrain the design: the
primary MCU's rotary encoder is rotation-only, and (confirmed via
`device.md`) there is **no other software-addressable button anywhere on
the board** — only a hardware power switch that cuts power entirely and
isn't controllable by firmware. There is no click, no long-press-a-button
gesture available at all.

## Decision

### Display power and lock are independent states

Display on/off is automatic and idle-timeout-driven (60s of no touch/
encoder activity), regardless of lock state. Lock/unlock is manual only,
triggered by a dedicated lock icon on the Now Playing screen. This
matches the two use cases directly (see
[`ux-guidelines.md` §6](../design/ux-guidelines.md#6-power-lock--status-flows)):
on a table, the device is never locked, it just dims; in a pocket, the
user locks it deliberately and it stays locked until manually unlocked
(no auto-re-lock timer).

A dark display wakes on any touch, but from the knob only after a
deliberate turn of about a quarter revolution
(`IdleTimer::kEncoderWakeDetents` = 8 of ~30 detents, net in one
direction, with no pause over 1.5 s between detents). A single nudge in a
pocket used to light the screen for a full minute and drained the battery
over a day.

The very first touch after the display was off is **swallowed entirely**
— not fed to LVGL, the gesture recognizer, or the lock's hold-to-unlock
detector — so waking the screen never also acts on whatever the finger
landed on (a button on the table, the unlock button in a pocket). The
user must touch again after the display visibly wakes.

### Unlock: hold the on-screen button while turning the encoder

With no button anywhere to click or long-press, unlocking uses a
two-factor gesture instead: hold an on-screen "unlock" button while
simultaneously rotating the encoder past a detent threshold
(`LockController::kUnlockDetentThreshold`, starting at 10, tunable on
hardware). Releasing the button before the threshold resets progress to
zero immediately, with no lingering partial credit. While locked, the
encoder does nothing else (no volume or list passthrough); only the
held-button-plus-rotation combination has any effect. See
[`ux-guidelines.md` §6](../design/ux-guidelines.md#6-power-lock--status-flows)
for why this gesture is deliberately effortful.

This is a new input primitive alongside ADR 0004's tap/swipe vocabulary,
but it is **not** added to `GestureRecognizer` — that class classifies
completed touch-up gestures (tap vs. swipe), which doesn't fit "sustained
press + concurrent rotation" at all. Instead, `power::LockController`
(pure logic, host-tested) exposes `onHoldStart`/`onHoldEncoderDelta`/
`onHoldEnd`, driven directly by the unlock button's own LVGL press/
release events and by encoder deltas from `main.cpp` — the same way
`InputRouter` is driven, just a separate path taken only while locked.

### The locked UI is an overlay, not a NavigationStack screen

Locking does not push a screen onto `TabController`'s active
`NavigationStack`. It's a separate `LockOverlay`, created once on LVGL's
top layer (`lv_layer_top()`, which LVGL always draws and hit-tests above
the active screen), shown or hidden purely by mirroring
`LockController::isLocked()`. `ScreenManager`/`NavigationStack`/
`TabController`/`InputRouter` are untouched by this feature.

This was chosen over pushing a `Locked` screen kind because that would
require: baking lock-awareness into `ScreenManager`'s dispatch; deciding
which of the two independent tab stacks to push onto; disabling
swipe-back while locked (which `TabController::handleSwipeBack()` has no
concept of); and exactly reversing whatever push logic was used on
unlock, in a stack with a fixed 8-entry depth cap already shared by real
navigation. An overlay avoids all of that — unlocking reveals exactly the
screen the user locked from, with zero navigation-state bookkeeping.

### `lib/power/`: two independent, host-tested state machines

- `IdleTimer` — display on/off only (`noteActivity`, `tick`,
  `isDisplayOn`).
- `LockController` — lock state and the hold+turn unlock state machine.

Kept as two classes rather than one `PowerController`, matching the
"independent states" decision above — merging them would recreate a
coupling the design explicitly avoids elsewhere. Neither touches LVGL or
hardware directly; `main.cpp` reads their state and drives
`St77916Driver::setBacklight()` and `LockOverlay` itself.

### `lib/ui-widgets/EdgeArc`: one reusable ring widget, two call sites

A thin wrapper around LVGL's native `lv_arc`, parameterized by
angle range/width/color and a min/max (or 0..1 fraction) value. Used for
the Now Playing volume indicator and `LockOverlay`'s unlock-progress
ring — the first two places a "value along the round edge" indicator was
needed, and reused rather than building each ad hoc. The volume ring is
**additive** alongside the existing linear bar for now; this screen's
layout has needed hardware-driven correction repeatedly (ADR 0004), so
the ring's on-device legibility gets verified before the bar is removed.

Alongside this, the repeated inline LVGL "create a button" boilerplate in
`ScreenManager.cpp` was factored into `lib/ui/LvglButtonHelpers.h`
(`makeIconButton` for click buttons, `makeHoldButton` for the unlock
button's press/release semantics), used by both `ScreenManager` and the
new `LockOverlay`.

### Icons: a small custom font instead of LVGL's built-in symbols

LVGL's built-in symbol font (`lv_symbol_def.h`) has no lock icon at all.
`lib/ui-widgets/IconFont.{h,c}` is a small custom LVGL font generated via
`lv_font_conv` from Google's Material Symbols Outlined variable font
(Apache License 2.0), containing only the two glyphs this feature needs
(`lock`, `lock_open`, Material Symbols codepoints U+E899/U+E898). The
rest of the app's buttons stay on `LV_SYMBOL_*` for now; extending the
custom font to more glyphs later is a matter of re-running the same
`lv_font_conv` command with a wider `--range`.

## Implementation status

`lib/power/IdleTimer.h` and `lib/power/LockController.h` are implemented
and host-tested (`test/test_power/`). `lib/ui-widgets/EdgeArc.h` and
`lib/ui/LvglButtonHelpers.h` are implemented; `ScreenManager` uses both
for the Now Playing screen's buttons, lock button, and volume ring.
`lib/ui/LockOverlay.h` implements the overlay, unlock button, and
progress ring. `src/main.cpp`'s `loop()` implements the interception
described above (idle-timeout tracking, wake-touch swallowing, and
routing encoder/touch to `LockController` vs. the normal
`InputRouter`/`GestureRecognizer` path depending on lock state).

Compiles clean for both `native` (all host tests pass) and `esp32-s3`.

**Real hardware bugs found and fixed during first on-device pass
(2026-09-13)**, each found by adding `lib/ui/LvglGlue::writeScreenshotToSerial()`
— a shadow full-frame buffer mirrored inside `flushCb` (LVGL only ever
flushes partial stripes) and dumped over Serial on a `SCREENSHOT`
command — and `scripts/screenshot.py`/`scripts/screenshot.sh` to decode
it into a BMP, rather than continuing from written descriptions or phone
photos:

- **`EdgeArc`'s ring rendered small and stuck in the display's top-left
  corner** instead of hugging the parent's edge. `lv_arc_create()`'s
  default size/position is a small fixed box at the parent's origin, not
  "fill the parent" — `EdgeArc::create()` never explicitly sized or
  centered the arc object itself. Fixed by sizing it to `LV_PCT(100)` of
  its host object and centering it.
- **The lock/unlock icon glyph never appeared** — a plain empty button.
  Root cause was two-fold, found via a screenshot-based diagnostic
  (a plain label with the icon font on a contrasting white background,
  outside any button, to isolate font-rendering from button/theme
  interaction): (1) the code applied the custom font to the label
  *after* `lv_label_set_text`/`lv_obj_center`, which left the glyph
  invisible even once the underlying font asset was fixed — the
  `makeIconButton`/`makeHoldButton` helpers now take an optional font
  parameter applied before text and centering; (2) `lv_font_conv`
  RLE-compresses glyph bitmaps by default, but this project's
  `lv_conf.h` has `LV_USE_FONT_COMPRESSED 0` (the decompressor is
  compiled out) — a compressed font's glyph metadata (size/advance) reads
  fine, sizing widgets correctly, but no pixel ever draws. Fixed by
  regenerating `IconFont.c` with `--no-compress`.
- **The volume ring and the unlock-progress ring were visually
  identical** (same blue), making them easy to mistake for each other —
  reported as "the unlock ring is always visible" when what was showing
  was the (intentionally always-visible-on-Now-Playing) volume ring.
  Fixed by giving them distinct colors (indigo for volume, green for
  unlock progress) plus a light grey background arc on both so the ring's
  full extent is visible at low values. *(Colors superseded 2026-09-13
  by the Braun palette — volume `accent`, unlock `confirm`, song progress
  `time`; see [UX guidelines §3](../design/ux-guidelines.md).)*
- **`LockOverlay`'s "Locked" label was invisible** — the app's light
  theme's default label text color is dark, meant for a light
  background, and the overlay's background is black. Same class of bug
  as ADR 0004's dark-on-dark list rows; fixed by setting the label's text
  color explicitly rather than inheriting the theme default.

**Volume display redesigned after first hands-on use (2026-09-13)**: the
originally additive linear bar (kept alongside the ring "until the ring
proved legible on hardware") is now removed entirely, and the ring
itself changed from always-visible to a temporary HUD:

- The linear bar/label are gone. `updateVolumeDisplay()` previously only
  updated the bar, never the ring — the ring looked present but did
  nothing when turning the knob, which is what surfaced this whole
  redesign rather than a smaller fix.
- The ring (+ a numeric label) now shows only while volume is actively
  being adjusted, auto-hiding `kVolumeHudTimeoutMs` (3000ms) after the
  last change (`ScreenManager::tickVolumeHud()`, called every `loop()`),
  the same interaction shape as a phone's volume overlay — matching user
  feedback that a permanently-occupied center-screen readout for a value
  "rarely being actively watched" wasn't worth the space.
- The ring's host is intentionally oversized beyond the screen
  (`kLcdHorRes/VerRes + 40`) and screens now clear
  `LV_OBJ_FLAG_SCROLLABLE` (see the two new AGENTS.md gotchas this
  produced: `lv_arc`'s knob-padding reservation, and LVGL's default
  screen scrollbar becoming visible with an oversized child) so the ring
  reaches the true round bezel edge rather than stopping short of it —
  a screenshot showed a persistent gap between the ring and the physical
  edge even once sized to exactly match the framebuffer.

**Still not verified on real hardware**: whether holding the unlock
button while turning the encoder is physically comfortable, whether 10
detents is the right threshold, the lock button's placement now that it
renders correctly (this screen's exact button layout — see
`ScreenManager::renderNowPlaying()` — is flagged as preliminary, matching
this project's established pattern of iterating this specific screen
after seeing it on the device), actual backlight on/off timing, and
whether the pocket-brushing-fabric assumption underlying the hold+turn
gesture actually holds up.

## Consequences

- `lib/power/` is entirely hardware-decoupled and host-testable, per
  [ADR 0003](0003-testing-strategy.md); only `LockOverlay`'s LVGL
  rendering and `main.cpp`'s wiring are hardware-facing and untested by
  the host suite (same category as `ScreenManager`).
- If a future hardware revision or wiring discovery adds a click to the
  encoder, the hold+turn gesture and the "no button anywhere" framing of
  this ADR should be revisited — it was specifically designed around that
  absence, same caveat as ADR 0004 made for the context-sensitive encoder
  mapping.
- The volume ring and linear bar are temporarily redundant on Now
  Playing; removing the bar once the ring is confirmed legible on
  hardware is expected, tracked as a small follow-up rather than done
  speculatively here.
