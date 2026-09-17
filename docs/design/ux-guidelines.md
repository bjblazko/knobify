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
   cached (the slot shows the live spectrum instead), navigation and utility controls drawn as quiet unfilled icons, and the
   battery indicator staying neutral until it actually needs attention.
6. **Good design is honest.** Indicators show only what's actually known
   — the battery indicator shows "Charging" only when the rail voltage proves USB power, the progress
   ring is hidden when a track's duration is unknown, titles come
   from tags rather than dressed-up filenames whenever tags exist, and the
   lowest brightness is still clearly lit, so it never looks like the
   display is off.
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
   lock screen is actually shown; the spectrum only while it's on screen,
   the display is on and the device is unlocked), and the device needs no cloud or
   account — music lives on a user-replaceable SD card.
10. **Good design is as little design as possible.** Prefer one reusable
    mechanism over several one-off effects — e.g. one `EdgeArc` widget
    serves the volume, unlock-progress and song-progress rings, and one
    button helper with three roles (§3a) draws every button on every
    screen; Settings is an ordinary list, not a new widget.

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
| `surface` | Snow White | `#F4F4F0` | Screen background everywhere, including the lock screen; text on dark elements |
| `surfaceAlt` | Light Grey | `#DCDDD8` | Unfilled ring tracks, secondary buttons, pressed state of quiet controls, mini-bar area |
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
neutrals don't — Snow White `#F4F4F0` arrives on the panel as neutral
`#F6F6F6`, and the panel itself shifts slightly toward green. Several
"warmed" surfaces were tried to compensate (`#EFEFE7`, `#E6E3D6`,
`#DEDBC6`, `#DED7C6`, `#E6D3BD`); each still looked wrongly tinted on the
device — cold, green-grey, or peach. None beat the plain original, so the
neutrals are back to Snow White `#F4F4F0` / Light Grey `#DCDDD8`: a
slightly cool neutral reads as intentional, a tint that's off doesn't.
Judge neutrals on the device, not on a monitor. The darker, warmer value also reduces glare on the reflective panel
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
5. **The brand mark carries no signal colour either.** Home's "knobify"
   wordmark is led by a small dial — an `ink` disc with the surface colour
   notched out as its indicator. A coloured bullet was tried first and was
   wrong for exactly the reason rule 2 gives: a logo means nothing in the
   functional sense, so it may not borrow a colour that does (the red it
   used is `warning`, which elsewhere means a flat battery). The mark
   earns its place through form instead — a knob seen from above, echoing
   the round display and the rotary encoder the way the circular transport
   buttons do.

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
  a fully rounded pill for the volume readout; 112px circles for the
  main menu tiles, `surfaceAlt` with an `ink` glyph, turning `ink` with a
  `surface` glyph when selected — the same selection color as a list row. The mini-bar is a
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
    pressed. Navigation and utility — back, lock, scan. A quiet *toggle*
    (shuffle, repeat) shows its on state as a `confirm` green glyph — a
    state, like the mini-bar glyph, never an accent.
- **Touch targets** are at least 44px in their smaller dimension, even when
  the visible glyph is smaller. Every button's hit area also extends 10px
  beyond its drawn bounds (`makeButton()` in `LvglButtonHelpers.h`), so
  keep at least ~20px between adjacent tappable controls to avoid
  overlapping hit areas.
- **A setting that can break touch** is never kept unconfirmed: apply it
  live, ask for a tap on a Primary "Keep" with a countdown ring, revert on
  timeout, and let the knob cancel (Touch calibration, ADR 0010).

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
  a particular screen as "the" permanent root, which is how the Home menu
  was added above the music tabs without restructuring navigation.
- **Home is the one screen with the wordmark.** It has no caption, title
  or back button, and it is the screen the device boots into — so it is
  where the product gets to say its name, and the only screen that does.
- **The device boots into the main menu** — a carousel of large round
  tiles (icon + label): Music, Audiobooks, Radio Plays, Settings, Sleep
  ([ADR 0010](../adr/0010-main-menu-and-settings.md),
  [ADR 0018](../adr/0018-collections-and-menu-visibility.md)). The selected
  destination sits in the middle at full size with its two neighbours
  shrunk and dimmed either side; a row of dots above says how many there
  are. The knob rotates, a tap on the centre opens, a tap on a neighbour
  rotates it in. The last-used destination stays selected. Adding one is
  one table row. Five destinations no longer all fit at once, which is why
  the dots are there — and why the user can hide the ones they do not
  have.
- **Which destinations Home shows is the user's choice.** Settings > Main
  menu toggles each one on or off. Settings itself can never be hidden
  (it is the only way back to that screen) and neither can the last
  visible entry; both refusals say why rather than leaving the row looking
  dead.
- **A collection is a shelf, not a mode.** Music, Audiobooks and Radio
  Plays are the same player pointed at a different SD folder, each with
  its own index, its own browse position and a small behaviour profile:
  spoken word has no Shuffle row, sorts by name, and resumes where a title
  was left. Nothing anywhere asks the user to "switch mode" — you open a
  shelf from Home and everything below it belongs to that shelf.
- **Settings is a list; each setting has its own screen** when it's set by
  the knob. A row ends in its current value as plain text. Brightness
  applies live while turning, with no confirm step. Maintenance actions
  (Rescan library) live here, not in content headers.
- **Two swipeable browse tabs per collection** — Library (tag-based
  Artist → Album → Track) and Files (folder browse within that
  collection's own root) — rather than a separate picker screen. A swipe
  never crosses collections: it means "the other view of what I am
  browsing", not "a different shelf".
- **One gesture, two meanings by context.** A left-right swipe pops the
  current screen if there's somewhere to go back to; otherwise (already
  at a tab root) the same gesture switches tabs. This avoids adding a
  dedicated picker UI and mirrors the same context-sensitivity principle
  applied to the encoder.
- **Context-sensitive encoder.** On browse screens, rotating scrolls the
  highlighted list item; on Now Playing, rotating adjusts volume (or
  shuttles through the track while the time pill is held, ADR 0013); on
  Home it moves the tile selection; on Brightness it sets brightness. The
  mapping is unambiguous per screen since no mode button exists to switch
  it explicitly.
- **Scope comes from where you start.** In Music, Artists, Albums and
  Tracks lists begin with a Shuffle row that shuffles the whole
  collection, the artist or the album. Spoken-word collections have no
  Shuffle row at all — shuffling an audiobook's chapters is never what
  anyone wants. Tapping a track plays its album in order and turns shuffle
  off. There is no scope setting ([ADR 0011](../adr/0011-shuffle-and-repeat.md)).
- **Shuffle and repeat are toggles in Now Playing's options panel**
  (the `...` handle at the bottom, [ADR 0014](../adr/0014-now-playing-options-panel.md)
  — an ellipsis, since a chevron there promised a direction and
  contradicted the down chevron that closes the same panel):
  glyphs `confirm` green while active. Repeat cycles off → all →
  one; only repeat is remembered across reboots. Spoken-word collections
  get no Shuffle toggle at all, judged by what is playing rather than by
  what is on screen behind the panel. A glyph alone didn't say
  which mode was active, so every tap also shows a message naming what now
  happens ("Shuffle on - album", "Repeat this track").
- **The cover slot switches from the options panel.** Its Cover /
  Spectrum button swaps the cover for the dot-matrix spectrum and back; the
  choice persists. Without a cover the spectrum always shows and the button
  is greyed out. The slot itself isn't tappable — nothing said it was
  (ADR 0014).
  No separate visualizer screen — it would need an undiscoverable gesture
  ([ADR 0009](../adr/0009-now-playing-spectrum-analyzer.md)).
- **Always-visible back button, in addition to swipe.** Swipe-to-back
  alone wasn't discoverable in real usage (a user reaching Now Playing had
  no visible way back at all) — a supplementary, always-visible quiet
  back affordance sits top-center rather than in a corner (§4, §7).
- **The back glyph says what going back means.** On list screens it is a
  left chevron (up one level; the caption names where you are). On the
  Library/Files roots it leads to the main menu; Home itself has none. On Now
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
  The lock ring's pulse (§6) and the Now Playing spectrum are the only
  non-transition animations, each justified because it carries meaning
  (an invitation to interact; the music's actual frequency content), not
  decoration.

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
  One exception (ADR 0013): while the time pill is held, a thin `ink`
  shuttle arc sits just inside the progress ring with a marker at the top
  — speed and position are both needed while scrubbing, and both vanish
  with the finger.
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
  below the header zone (back button + caption) and ends above the
  mini-bar, instead of spanning the full screen with padding — rows
  sliding under the back button collided with it visually.
- **List screens say where you are.** A small caption under the top
  control names the current context (artist, album, folder), since the
  top of a round screen is too narrow to be useful for rows anyway. At a
  collection's root that caption is the collection's own name — with
  three shelves, "Library" no longer says which one you are on.
- **Secondary facts are plain text, never badges.** Album rows end in the
  release year, the Brightness row in its percentage, a Main menu row in
  "On"/"Off" (14px, `structure`;
  `surfaceAlt` on the `ink` selected row). The year explains the
  chronological sort. The row's own text color at reduced opacity was
  tried first and came out barely readable on the selected row. No pills or overlays: they cost width
  on a narrow round screen and compete with the selected row. Nothing is
  shown when the year is unknown, and no release type (album/EP/single)
  is shown at all, since no reliable tag exists and guessing from track
  count would be dishonest.
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
  line; the Now Playing title gets one, since the cover slot (cover or
  spectrum) is always there. Whatever follows a variable-height text is positioned from
  its actual height, never a fixed offset.
- **Minimalism in widgets.** Build a widget for its current, real use
  case (e.g. `EdgeArc` as a thin config+create/setValue wrapper); extend
  it only once a genuine further use case needs more, rather than
  generalizing speculatively.
- **No placeholder for absent data.** When information isn't available
  (e.g. no cached album art), show nothing rather than a placeholder —
  the remaining content moves up to fill the space instead. The Now
  Playing cover slot is the exception: without a cover it shows the live
  spectrum, which is real data rather than a placeholder.
- **Messages appear briefly, one at a time, in plain words.** The message
  area (`ui_widgets::MessageArea`) is an `ink` pill with 16px `surface`
  text, like the volume readout: no icon, no signal color. It stays ~2 s,
  a new message replaces the current one, and it never takes a tap. Each
  screen anchors it by center point clear of the bezel and near what caused
  it (Now Playing: the cover slot's center). Its width is capped to the
  round screen's width at that height, with an ellipsis instead of clipping.
  It lives on the top layer, so it survives re-renders. *Screen* messages
  go away when the screen changes or the device locks; *system* messages
  (future: e.g. a lost connection) stay, and belong in the top-center
  caption zone. The volume readout takes the spot over a message.
- **The spectrum is a neutral dot matrix.** 12×12 round dots, `ink` lit
  and `surfaceAlt` unlit, filling the cover's 96px square; no signal
  colors, gradients or peak markers. Paused audio decays to zero rather
  than freezing.

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

The 2026-09-13 redesign's placement questions (song-progress ring
visibility at the edge, Now Playing offsets with and without a cover)
were confirmed on the physical device — see
[ADR 0008](../adr/0008-braun-design-system-and-screen-redesign.md).

Non-ASCII tag text (umlauts, accents, `·`, `×`) was confirmed on the
physical device on 2026-09-17, once the project's own text fonts and
UTF-8 tag decoding landed — see
[ADR 0019](../adr/0019-utf8-tag-text-and-project-text-fonts.md).
