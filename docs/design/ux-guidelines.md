# UX/UI Design Guidelines

This is the canonical reference for knobify's UX/UI design philosophy,
visual language, and interaction flows — the "why" behind the screens.
Architecture and implementation decisions (which classes, which files,
what was verified on real hardware, what's still open) continue to live
in the ADRs; each section below links back to the ADR that encodes it in
detail.

## 1. Purpose

Before writing UI code or making a layout call, this document should
already answer "does knobify already have a stance on this?" It exists so
that design intent isn't only discoverable by reading ADR prose written
for a specific decision, or by tracing rationale through source comments.

This document must itself follow principle 6 (honest): anything
described here is either implemented, or explicitly marked *decided, not
yet implemented*.

## 2. Design Philosophy — Dieter Rams' Ten Principles

knobify's UI decisions are read through Dieter Rams' ten principles of
good design. Each is listed with a short note on how it shows up in the
product.

1. **Good design is innovative.** Solutions should fit *this* device —
   e.g. the hold-button-while-turning-encoder unlock gesture exists
   because this board has no click button at all, not because it's a
   trend (§5, §6).
2. **Good design makes a product useful.** Every screen decision starts
   from the two real usage contexts knobify is actually used in: on a
   table, and in a pocket while playing (§6).
3. **Good design is aesthetic.** Visual quality is not an afterthought —
   one palette (§3) and one typographic scale, shape vocabulary and
   control hierarchy (§3a) apply to every screen.
4. **Good design makes a product understandable.** The UI's structure
   should be self-explanatory: context-sensitive controls (§5) instead of
   modes that must be remembered, a caption telling you *where* you are
   in a list (§7), and exactly one visually dominant control per screen
   telling you *what* the main action is (§3a).
5. **Good design is unobtrusive.** Widgets serve the content, not the
   other way round — e.g. no album-art placeholder when no cover is
   cached, navigation and utility controls drawn as quiet unfilled icons, and the
   battery indicator staying neutral until it actually needs attention.
6. **Good design is honest.** Indicators show only what's actually known
   — the battery indicator never fakes a charging state, the progress
   ring is hidden when a track's duration is unknown, and titles come
   from tags rather than dressed-up filenames whenever tags exist.
7. **Good design is long-lasting.** No fashionable effects (gradients,
   glows, shadows, glassmorphism). The form vocabulary is deliberately
   timeless: a neutral surface, one typeface, the circle (echoing the
   round device itself) and the rounded rectangle.
8. **Good design is thorough down to the last detail.** The round bezel's
   corner-clipping and edge behavior has been re-learned and fixed
   repeatedly across nearly every screen (§4, §7) — sweating exactly
   these details is treated as core to the job, not polish to defer.
9. **Good design is environmentally friendly.** On a battery device this
   means energy and longevity: the display powers off when idle (§6), no
   animation runs perpetually (the lock ring's pulse only runs while the
   lock screen is actually shown), and the device needs no cloud or
   account — music lives on a user-replaceable SD card.
10. **Good design is as little design as possible.** Prefer one reusable
    mechanism over several one-off effects — e.g. one `EdgeArc` widget
    serves the volume, unlock-progress and song-progress rings, and one
    button helper with three roles (§3a) draws every button on every
    screen.

## 3. Color System — Braun-Inspired Palette

### Concept

Color is not decoration in this system — it exists solely to serve
function, signaling, and operability, in the tradition of Dieter Rams'
work for Braun. A UI should read correctly even if every accent color
were removed; color then adds meaning on top, sparingly.

- **90% neutral base** (off-white / light grey)
- **9% structure & contrast** (anthracite / matte black)
- **1% signal / accent** (orange, yellow, green, red)

### Palette

Every color maps 1:1 to a token in `lib/ui/Theme.h` — UI code uses the
token, never a raw hex value or an LVGL palette color.

**Neutral base & structure:**

| Token | Name | Hex | Use |
|---|---|---|---|
| `surface` | Warm Snow White | `#E6D3BD` | Screen background everywhere, including the lock screen; text on dark elements |
| `surfaceAlt` | Warm Light Grey | `#D6C3AD` | Unfilled ring tracks, secondary buttons, pressed state of quiet controls, mini-bar area |
| `structure` | Mid Anthracite | `#4A4C4E` | Secondary text (captions, time), quiet icons, battery |
| `ink` | Matte Black | `#1E1F21` | Primary text, selected list row, volume readout pill |

**Signal colors** (functional elements only):

| Token | Name | Hex | Meaning |
|---|---|---|---|
| `accent` | Orange Signal | `#E85D04` | The single primary action on a screen (Play/Pause, Unlock); the volume ring while it is being set |
| `confirm` | Functional Green | `#2A8C4A` | Confirmation / active state — unlock progress, mini-bar playback glyph |
| `time` | Braun Yellow | `#F5AA1C` | Time passing — the song-progress ring only (after the yellow second hand of Braun clocks) |
| `warning` | Accent Red | `#D62828` | Needs attention — low/empty battery |

Braun Yellow (`#F5AA1C`) is *not* a second call-to-action color: two
"primary" colors (yellow and orange) would compete for the same meaning,
and yellow controls read weakly against the off-white surface. It is
kept for exactly one meaning — time passing, like the yellow second hand
of Braun's clocks — on the song-progress ring, where it sits at the bezel
next to the dark housing and reads clearly (an anthracite ring blended
into the case).

### RGB565: pick colors the panel can actually show

The display is 16-bit RGB565, which rounds every color to 32 red/blue and
64 green steps. Saturated signal colors survive this fine, but subtle
neutrals don't — the original Snow White `#F4F4F0` arrived on the panel as
neutral `#F6F6F6`, losing exactly the warmth that made it Snow White.
Neutral tokens are therefore exact RGB565 values (verified by sampling a
serial screenshot), and the surface is dimmed and warmed well beyond the
nominal Snow White — `#EFEFE7` and then `#E6E3D6` still looked cold and
bright on the real panel, and `#DEDBC6` (red and green nearly equal)
looked green-tinted — a warm neutral needs green clearly *between* red
and blue. The panel itself shifts toward green, so values that read as a
warm off-white on the device look distinctly peach on a monitor. The darker, warmer value also reduces glare on the reflective panel
and is closer to a matte Braun housing. Judge neutrals on the device, not
on a monitor.

### Application rules

1. **Restraint** — at most one permanently visible accent (orange)
   element per screen; the transient volume ring is the one exception,
   since it only exists while the user is acting. Yellow, green and red
   only ever appear as signals, never as decoration.
2. **Functional separation** — color signals clickability or state, never
   just decoration. If a color doesn't mean something, don't add it.
3. **Similar-looking indicators get distinct colors by meaning.** The
   three edge rings look alike, so each carries its meaning in its color:
   song progress `time` (time passing), volume `accent` (a value you are
   actively setting), unlock progress `confirm`. This — not
   restraint — is why they differ (§6).
4. **No noise** — neutrals stay matte/desaturated; no shadows or
   gradients.

### Why a light theme

This display is a reflective IPS LCD, not an OLED — a dark theme (tried
first) rendered poorly, and black pixels save no power on an LCD
backlight. Every screen, the lock screen included, uses `surface`.

## 3a. Typography, Shape & Control Hierarchy

- **One typeface, four sizes** (Montserrat, LVGL built-in):
  14 caption/secondary (artist line, time, list caption, battery) ·
  16 body (mini-bar, hints) · 20 title (list rows, track title, "Locked") ·
  28 numeral & primary glyph (volume readout, Play/Pause icon).
- **Selection and action never share a color.** The knob-selected list
  row is `ink`; the always-present mini-bar is a `surfaceAlt` area
  with a small `confirm` glyph (green = active/running) — an ink mini-bar read as
  a second selected row, and an accent one was too loud for something
  permanently on screen. A pale green *tinted area* was also rejected:
  Braun keeps surfaces neutral and uses color only on small functional
  details.
- **Shapes.** Circles for the round, thumb-operated controls (transport,
  unlock) — echoing the device's own form. 12px radius for list rows;
  a fully rounded pill for the volume readout. The mini-bar is a
  full-width bottom area that the round bezel cuts into a circle segment
  — letting the bezel shape an element is fine when its *content* stays
  inside the visible circle.
- **Control hierarchy — three button roles, nothing else:**
  - *Primary*: a filled `accent` circle with a white glyph. Exactly one per
    screen — it is the answer to "what does this screen do?".
  - *Secondary*: a filled `surfaceAlt` circle with an `ink` glyph.
    Companions of the primary control — previous/next beside Play/Pause,
    like the grey keys beside the one colored key on a Braun tape deck.
  - *Quiet*: no fill, `structure` glyph, `surfaceAlt` background only while
    pressed. Navigation and utility — back, lock, scan.
- **Touch targets** are at least 44px in their smaller dimension, even when
  the visible glyph is smaller.

## 4. Hardware Constraints That Drive Every Screen

Two hardware facts constrain every layout and interaction decision (see
[ADR 0004](../adr/0004-navigation-library-and-index-architecture.md)'s
Context section):

- **The display is physically round** — a 360×360 square framebuffer
  behind a round bezel that clips the corners. Corner-anchored UI
  (fixed back buttons, corner-placed icons) is never safe. This has been
  relearned on real hardware repeatedly: the default list scrollbar, the
  first list row, the mini-bar, the back button, and the battery
  indicator all had to move off literal edges/corners after looking fine
  in a screenshot but being invisible on the physical device (§7).
- **The rotary encoder is rotation-only** — there is no click or push
  button anywhere on the board (confirmed via `device.md`; only a
  hardware power switch exists, and it isn't software-addressable). What
  a knob turn *means*, and how any "hold" gesture is expressed, must
  always be inferred from context — never from an explicit mode toggle or
  a button press.

## 5. Navigation Model & Gesture Flows

Full architecture: [ADR 0004](../adr/0004-navigation-library-and-index-architecture.md).

- **Injectable-root screen stack.** The navigation stack never hardcodes
  a particular screen as "the" permanent root, so a future Home/menu
  screen can be inserted without restructuring navigation.
- **Two swipeable top-level tabs** — Library (tag-based Artist → Album →
  Track) and Files (raw folder browse) — rather than a separate picker
  screen.
- **One gesture, two meanings by context.** A left-right swipe pops the
  current screen if there's somewhere to go back to; otherwise (already
  at a tab root) the same gesture switches tabs. This avoids adding a
  dedicated picker UI and mirrors the same context-sensitivity principle
  applied to the encoder.
- **Context-sensitive encoder.** On browse screens, rotating scrolls the
  highlighted list item; on Now Playing, rotating adjusts volume. The
  mapping is unambiguous per screen since no mode button exists to switch
  it explicitly.
- **Always-visible back button, in addition to swipe.** Swipe-to-back
  alone wasn't discoverable in real usage (a user reaching Now Playing had
  no visible way back at all) — a supplementary, always-visible quiet
  back affordance sits top-center rather than in a corner (§4, §7).
- **The back glyph says what going back means.** On list screens it is a
  left chevron (up one level; the caption names where you are). On Now
  Playing it is a down chevron — "collapse the player" into the list's
  mini-bar. A left chevron there sat right above the previous-track
  button, read like "previous", and didn't say where it led.
- **Gesture discoverability via a one-time nudge, not a persistent icon.**
  *Decided, not yet implemented.* Each gesture (swipe-to-back,
  swipe-to-switch-tab) is to be taught by briefly animating the screen
  content in that direction, shown once *ever* per gesture type (tracked
  via a persisted flag), not on every screen visit or every boot.
- **Animation is scoped to screen transitions only, for v1.** *Decided,
  not yet implemented* — screens currently switch instantly. Push/pop/
  tab-switch transitions are to slide in the gesture's direction, reusing
  one mechanism everywhere rather than building several one-off effects.
  The lock ring's pulse (§6) is the one existing non-transition
  animation, justified because it carries meaning (an invitation to
  interact), not decoration.

## 6. Power, Lock & Status Flows

Full architecture: [ADR 0005](../adr/0005-power-lock-and-round-edge-indicators.md),
[ADR 0007](../adr/0007-battery-indicator.md).

- **Two independent states mapped to two real usage contexts.** Display
  power (automatic, idle-timeout-driven) and lock (manual, deliberate) are
  independent: on a table the device is never locked, it just dims; in a
  pocket the user locks it deliberately and it stays locked until
  manually unlocked.
- **Wake never also acts.** The very first touch after the display was
  off is swallowed entirely — not fed to LVGL, the gesture recognizer, or
  the unlock detector — so waking the screen never also triggers whatever
  the finger landed on.
- **Unlock is deliberately effortful.** With no button anywhere to click
  or long-press, unlocking requires holding an on-screen button while
  simultaneously turning the encoder past a threshold; releasing early
  resets progress to zero immediately, with no partial credit. This
  friction is intentional — it needs to be very unlikely to happen from a
  device brushing against pocket fabric.
- **Hints combine a wordless cue with a text label.** Neither a pulsing
  ring alone nor a button label alone made the hold-and-turn gesture
  guessable in practice — the two together do. The ring's pulse itself
  pauses while actually holding (real progress becomes the feedback
  instead), keeping the invitation-to-interact and the in-progress
  feedback visually distinct.
- **Edge rings use signal colors, not neutrals.** The rings sit right at
  the bezel, next to the dark housing — anthracite and black rings
  blended into the case.
- **Edge rings carry meaning in their color** (§3 rule 3): song progress
  `time` yellow, volume `accent` orange, unlock progress `confirm` green —
  each on a visible
  `surfaceAlt` track.
- **One edge ring visible at a time.** On Now Playing the thin song-
  progress ring is the resting state; while volume is being adjusted the
  volume ring temporarily replaces it, then hands back. Two concentric
  rings at once would be noise.
- **Song progress is shown only when known.** The progress ring is hidden
  if the decoder reports no duration — no fabricated progress.
- **Volume feedback follows a phone's volume-HUD shape.** Volume shows as
  a ring + numeric readout only while actively being adjusted, auto-
  hiding shortly after the last change — a permanently-occupied
  center-screen readout for a value that's rarely being actively watched
  wasn't worth the space.
- **Status indicators live above every screen, not per-screen.** Battery
  level (no charging state — no real signal exists to detect it, and a
  fabricated one would be actively misleading) is rendered once on LVGL's
  top layer rather than added to every individual screen.
- **Status is shown only when it matters.** During normal use the battery
  indicator is hidden; at 20% or below it appears as a red icon +
  percentage in the caption slot under the top control. On the lock
  screen — the natural moment to glance at status, one tap away via the
  lock button — it is always shown, neutral unless low, above "Locked".
  An always-on icon beside the back/scan button (the earlier design) was
  the only off-axis element on screen and read like a tappable toolbar
  icon.
- **Status indicators are corner-safe by placement, not by luck.** The
  battery indicator is centered on the vertical axis — an initial corner
  placement looked correct in a screenshot but was invisible on the
  physical device (§4, §7).

## 7. Layout & Widget Principles

- **Never anchor UI at a corner.** The round bezel clips corners far more
  aggressively than top-/bottom-center placement. This has been the
  single most repeated lesson across every screen built so far.
- **A screenshot alone is not proof of on-device visibility.** Anything
  placed near an edge must be confirmed on the physical board — a
  serial-dumped screenshot captures the raw square framebuffer, not what
  the round bezel actually shows.
- **Content never scrolls underneath fixed controls.** A list starts
  below the header zone (back/scan button + caption) and ends above the
  mini-bar, instead of spanning the full screen with padding — rows
  sliding under the back button collided with it visually.
- **List screens say where you are.** A small caption under the top
  control names the current context (artist, album, folder), since the
  top of a round screen is too narrow to be useful for rows anyway.
- **Show tag data, not filenames.** Track titles, artists and albums come
  from the library's tags; the filename (without extension) is only the
  fallback when no tag exists (e.g. untagged files in the Files tab).
- **Filled shapes need symmetric insets.** Plain left-to-right text can
  get away with a larger leading than trailing inset (the ellipsis end is
  less legibility-critical), but a filled element such as the selected
  list row shows both ends — so rows are inset equally on both sides,
  keeping the whole shape inside the bezel's circle at its height.
- **Edge-hugging rings are deliberately oversized and let the bezel clip
  them.** An arc sized to exactly match the screen still leaves a visible
  gap from the true round edge — oversizing beyond the framebuffer and
  letting the physical bezel clip the excess is what actually reaches the
  edge.
- **Prefer static truncation over marquee scrolling.** A bigger font with
  a clean ellipsis reads better on a small round display than several
  long names scrolling mid-marquee at once. Every text has an explicit
  line budget: list rows, captions, mini-bar and the artist line get one
  line; the Now Playing title gets two when no cover is shown, one
  otherwise. Whatever follows a variable-height text is positioned from
  its actual height, never a fixed offset.
- **Minimalism in widgets.** Build a widget for its current, real use
  case (e.g. `EdgeArc` as a thin config+create/setValue wrapper); extend
  it only once a genuine further use case needs more, rather than
  generalizing speculatively.
- **No placeholder for absent data.** When information isn't available
  (e.g. no cached album art), show nothing rather than a placeholder —
  the remaining content moves up to fill the space instead.

## 8. Scope Boundaries

Future/out-of-scope UX ideas (theming, jog-dial scrubbing, a settings
screen, general visual-polish passes, etc.) are tracked in a single place:
README.md's ["Explicitly out of scope for now"](../../README.md#explicitly-out-of-scope-for-now)
list. Don't duplicate that list here — check it before proposing new
scope, and add new backlog ideas there, not in this document.

## 9. Open Design Questions

Carried from [ADR 0005](../adr/0005-power-lock-and-round-edge-indicators.md)'s
"Still not verified on real hardware" section (authoritative there —
listed here for visibility):

- Whether holding the unlock button while turning the encoder is
  physically comfortable in practice.
- Whether 10 detents is the right unlock threshold.
- Whether the pocket-brushing-fabric assumption underlying the hold+turn
  gesture actually holds up in real pocket use.

Raised by the 2026-09-13 redesign (need confirmation on the physical
bezel, not a screenshot):

- Whether the thin song-progress ring stays visible at the round edge.
- Final vertical offsets on Now Playing (cover, title block, transport
  row, quiet lock button at bottom-center).
