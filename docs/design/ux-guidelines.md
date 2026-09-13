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

## 2. Design Philosophy — Dieter Rams' Ten Principles

knobify's UI decisions are read through Dieter Rams' ten principles of
good design. Each is listed with a short note on how it already shows up
in the product today.

1. **Good design is innovative.** Solutions should fit *this* device —
   e.g. the hold-button-while-turning-encoder unlock gesture exists
   because this board has no click button at all, not because it's a
   trend (§5, §6).
2. **Good design makes a product useful.** Every screen decision starts
   from the two real usage contexts knobify is actually used in: on a
   table, and in a pocket while playing (§6).
3. **Good design is aesthetic.** Visual quality is not an afterthought —
   see the Color System (§3) as the intended long-term visual language.
4. **Good design makes a product understandable.** The UI's structure
   should be self-explanatory: context-sensitive controls (§5) instead of
   modes that must be remembered, and gestures taught once via a brief
   animated hint rather than a manual (§5).
5. **Good design is unobtrusive.** Widgets serve the content, not the
   other way round — e.g. the Now Playing screen shows no album-art
   placeholder when no cover is cached, matching the screen's existing
   minimalism rather than filling the gap with decoration.
6. **Good design is honest.** Indicators show only what's actually known
   — e.g. the battery indicator shows charge level but never fakes a
   charging state, since no real signal exists for it.
7. **Good design is long-lasting.** Widgets are built for their current,
   real use case and extended later only when a genuine second need
   appears (e.g. `EdgeArc`), rather than speculatively generalized up
   front.
8. **Good design is thorough down to the last detail.** The round bezel's
   corner-clipping and edge behavior has been re-learned and fixed
   repeatedly across nearly every screen (§4, §7) — sweating exactly
   these details is treated as core to the job, not polish to defer.
9. **Good design is environmentally friendly.** Not yet a live driver of
   any decision in this project; kept in the list because it's part of
   the source philosophy this document follows.
10. **Good design is as little design as possible.** Prefer one reusable
    mechanism over several one-off effects — e.g. screen-transition slide
    animation is the one animation investment scoped for v1, reused for
    every push/pop/tab-switch rather than building a different effect per
    screen (§5).

## 3. Color System — Braun-Inspired Palette

### Concept

Color is not decoration in this system — it exists solely to serve
function, signaling, and operability, in the tradition of Dieter Rams'
work for Braun. A UI should read correctly even if every accent color
were removed; color then adds meaning on top, sparingly.

- **90% neutral base** (white / light grey)
- **9% structure & contrast** (dark grey / black)
- **1% signal / accent** (yellow, orange, green, red)

### Palette

**Neutral base** (housing & surfaces):

| Name | Hex | Use |
|---|---|---|
| Snow White | `#F4F4F0` | Primary surface color — a warm off-white, not stark white |
| Light Grey | `#DCDDD8` | Secondary surfaces, panels, backgrounds |
| Mid Anthracite | `#4A4C4E` | Dark structural elements, scales, dividers, contrast text |
| Matte Black | `#1E1F21` | High-contrast panels, control surfaces |

**Signal & accent colors** (functional elements only):

| Name | Hex | Use |
|---|---|---|
| Braun Yellow | `#F5AA1C` | Primary call-to-action — power/activate, the single most important interactive point on a screen |
| Orange Signal | `#E85D04` | Main switches, key interaction points |
| Functional Green | `#2A8C4A` | Confirmation, active/operating state, volume/level feedback |
| Accent Red | `#D62828` | Record/stop, warnings |

### Application rules

1. **Restraint** — at most one accent color per UI context/screen. Two
   competing accents read as noise, not signal (this is also why the
   volume ring and the unlock-progress ring were deliberately given
   different accent colors rather than sharing one — see §6).
2. **Functional separation** — color signals clickability or state, never
   just decoration. If a color doesn't mean something, don't add it.
3. **No noise** — neutrals stay matte/desaturated. Avoid bright or
   saturated tones outside the four signal colors above.

### Status: adopted, not yet applied on-device

This palette is the design language knobify is adopting going forward.
The current on-device theme (a light LVGL theme with an indigo accent,
chosen 2026-09-12 — see `lib/ui/LvglGlue.cpp` — because this display is a
reflective IPS LCD that didn't render an earlier dark theme well) predates
this palette and has not yet been reconciled against it. Bringing the
running UI in line with this palette is a separate future implementation
task, not covered by this document's consolidation.

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
  no visible way back at all) — a supplementary, always-visible back
  affordance was added, positioned top-center rather than a corner (§4,
  §7).
- **Gesture discoverability via a one-time nudge, not a persistent icon.**
  Each gesture (swipe-to-back, swipe-to-switch-tab) is taught by briefly
  animating the screen content in that direction, shown once *ever* per
  gesture type (tracked via a persisted flag), not on every screen visit
  or every boot.
- **Animation is scoped to screen transitions only, for v1.** Push/pop/
  tab-switch transitions slide in the gesture's direction, reusing one
  mechanism everywhere rather than building several one-off effects — the
  one animation investment made for v1 (further ideas, e.g. Now Playing
  flourishes, are deliberately deferred).

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
- **Distinct accent colors prevent two rings being confused.** The Now
  Playing volume ring and the lock screen's unlock-progress ring look
  superficially similar (both are edge-hugging arcs), so they're given
  different accent colors (indigo for volume, green for unlock progress)
  plus a visible background track at both, matching the "one accent
  reads clearly" rule in §3.
- **Volume feedback follows a phone's volume-HUD shape.** Volume shows as
  a ring + numeric readout only while actively being adjusted, auto-
  hiding shortly after the last change — a permanently-occupied
  center-screen readout for a value that's rarely being actively watched
  wasn't worth the space.
- **Status indicators live above every screen, not per-screen.** Battery
  level (color-coded, no charging state — no real signal exists to detect
  it, and a fabricated one would be actively misleading) is rendered once
  on LVGL's top layer so it's visible everywhere, including the lock
  overlay, rather than being added to every individual screen.
- **Status indicators are corner-safe by placement, not by luck.** The
  battery icon sits on the same row as the back/scan button (top-center),
  not a screen corner — an initial corner placement looked correct in a
  screenshot but was invisible on the physical device (§4, §7).

## 7. Layout & Widget Principles

- **Never anchor UI at a corner.** The round bezel clips corners far more
  aggressively than top-/bottom-center placement. This has been the
  single most repeated lesson across every screen built so far.
- **A screenshot alone is not proof of on-device visibility.** Anything
  placed near an edge must be confirmed on the physical board — a
  serial-dumped screenshot captures the raw square framebuffer, not what
  the round bezel actually shows.
- **Asymmetric insets for left-to-right text.** Padding the leading edge
  more than the trailing edge keeps the start of each row's text clear of
  the round bezel's curve, without wasting space on the (less
  legibility-critical) trailing/ellipsis end.
- **Edge-hugging rings are deliberately oversized and let the bezel clip
  them.** An arc sized to exactly match the screen still leaves a visible
  gap from the true round edge — oversizing beyond the framebuffer and
  letting the physical bezel clip the excess is what actually reaches the
  edge.
- **Prefer static truncation over marquee scrolling.** A bigger font with
  a clean ellipsis reads better on a small round display than several
  long names scrolling mid-marquee at once.
- **Minimalism in widgets.** Build a widget for its current, real use
  case (e.g. `EdgeArc` as a thin config+create/setValue wrapper); extend
  it only once a genuine second use case needs more, rather than
  generalizing speculatively.
- **No placeholder for absent data.** When information isn't available
  (e.g. no cached album art), show nothing rather than a placeholder —
  matching a screen's existing minimalism rather than adding decoration
  to fill a gap.

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
- The lock button's final placement on the Now Playing screen (currently
  flagged as preliminary; this screen's layout has needed repeated
  hardware-driven adjustment).
- Whether the pocket-brushing-fabric assumption underlying the hold+turn
  gesture actually holds up in real pocket use.
