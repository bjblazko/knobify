# 0021: Jump by letter, and browsing Music by more than artist

## Status

Accepted — 2026-09-17. Extends ADR 0004's navigation model and ADR 0018's
collection parameterisation; bumps the `IndexCache` format of ADR 0004 to
v5 (one full rescan).

## Context

Browsing costs one detent per row. On a real library (~100 artists)
reaching "W" from "A" is a hundred turns of the knob — the shelf is
sorted, but nothing lets you skip along it. "Jump-by-letter navigation for
long lists" had sat in the README backlog since v1.

The obvious trigger, a click of the encoder, does not exist: this board's
rotary encoder has **no push button** (device.md). So the trigger has to
be touch, and the device already has one vocabulary for "the knob means
something else right now" — holding the time pill turns it into a shuttle
(ADR 0013).

The same conversation raised a second want: Music should be reachable by
more than the artist shelf — by album name, song name, year and genre. Those
shelves are exactly the long, flat, name-sorted lists that are unusable
without letter jumping, so the two decisions belong together. Genre existed
nowhere: no tag parser read it and the index had no field for it.

## Decision

### Jump by letter is a mode the header owns, and it closes itself

Pressing the context caption turns the knob into a letter dial. Each detent
moves the highlight to the first row of the next initial-letter group, and
the caption shows that letter. The mode ends on a second press, on a row
tap, on leaving the screen, or on going unused — the highlight stays where
it landed.

**Hold-and-turn is the gesture, the same one the time pill's shuttle
already teaches (ADR 0013).** This was decided the other way first — a tap
to enter a mode, on the reasoning that letter jumping takes several turns
and a careful landing, so a sustained hold would be tiring. On the device
the hands disagreed immediately: the user pressed the chip and turned the
knob with the other hand, which is exactly what the shuttle taught them,
and nothing happened, because the mode opened on the *release* of a click.
So the chip opens the mode on the **press**, and cannot time out while a
finger is on it.

Releasing leaves the mode open, so a plain tap works too, and a second
press closes it. That keeps both gestures without a mode switch anywhere.

The idle timeout is two timeouts: 5 s before the first turn, 1.5 s between
turns. One value could not be both. A mode opened by a finger on the glass
is used by a hand on the knob, and moving from one to the other takes
longer than 1.5 s — a serial trace showed fourteen consecutive taps each
opening and closing perfectly, with no turn ever arriving inside the
window (2026-09-17). Once a hand is on the knob, the short timeout is what
keeps the mode from lingering.

The timeouts are also what make the mode safe to enter: there is no state
to get stuck in, and no way to leave it wrong.

It lives entirely in the 22 px header band, as the caption itself: no
overlay, no scrim. Nothing is covered, no tap is swallowed, and the rows
stay live throughout. **The list is the feedback; the letter is only
confirmation** — which is also why the affordance is drawn only where it
earns its place: a name-ordered list of at least 20 rows with more than one
letter in it. Tracks (track-number order), Music's albums (year order),
Years (numeric) and every settings list show a plain caption, as before.

### Buckets are letter boundaries, not an A–Z index

A bucket starts wherever a row's letter differs from the row above it. A
Files listing — folders A…Z, then files A…Z — therefore needs no special
case: you step through the boundaries in list order. Digits, symbols and
non-Latin scripts collapse into one `#` group.

The letter comes from the key the list is actually sorted by
(`LibraryIndex::nameSortKey()`, renamed from `artistSortKey()` now that
album and song shelves use it too), so "The Cure" jumps under C and "Die
Ärzte" under A — exactly where the rows are. Deriving the letter any other
way would let the header disagree with the list.

`lib/navigation/LetterJump.h` holds all of it as pure logic over explicit
timestamps, like `NavigationStack` and `Shuttle`: no LVGL, host-tested.

### Music's shelf is a property of the tab, chosen from a leading row

The Library tab's root becomes whichever of five shelves you pick —
Artists, Albums, Songs, Years, Genres — via a "Browse by" row in the slot
Shuffle and Continue already use, and the choice is remembered in NVS
(`musicAxis`). Music still opens on Artists until you say otherwise, so the
common path costs no extra tap.

Re-rooting (`TabController::setLibraryRoot()`) drops everything above the
root: the artist you were inside means nothing on a year shelf. Only Music
offers this; spoken-word collections keep their shelves (ADR 0018).

One screen kind, `AlbumsFlat`, covers every album shelf — all albums, one
year's, one genre's — because only the filter in `ScreenParams` differs.
Years and Genres lead into it; it leads into Tracks. Songs plays straight
from the shelf, queueing it in the order shown.

Shuffle on a filtered shelf records `PlayScope::Library`, not a shelf of
its own: `PlayScope` is what a resume record rebuilds a queue from, and it
cannot reconstruct "1994". After a reboot such a queue continues as the
whole library rather than claiming a shelf it cannot rebuild.

### Genre lives on the album, and costs a rescan

`Album.genreId` indexes a genre table interned by folded key, so "Rock",
"rock" and "ROCK" are one shelf. Id 0 is the "unknown" entry every index
starts with, so an untagged album needs no separate flag and is never
offered a shelf. The first track of an album that carries a genre names it;
later tracks of a mixed compilation do not move it. Per-track genre would
be more faithful for compilations and was rejected as more index for a
shelf nobody browses per track.

All four parsers read it: ID3v2 `TCON` (plus the ID3v1 trailer's genre
byte), MP4 `©gen` and `gnre`, Vorbis `GENRE`, RIFF `IGNR`. Three of those
still refer to genres by number, so one shared `Id3Genres.h` table resolves
them; a refining text ("(17)Hard Rock") wins over its number, and an
unknown number resolves to nothing rather than to a misleading name.

`IndexCache` goes to format **v5** (genre table appended, `genreId` on each
album). A v4 cache fails its version check and takes the existing rescan
path — no migration code, one full rescan of the card.

## Consequences

- One full rescan on the first boot after this lands.
- A reboot cuts a Library back-stack to its root whenever the stack is
  deeper than the shelf itself: `NavigationResumeSource` drops screen kinds
  past `NowPlaying`, which is where the new kinds had to be appended
  (records store kinds by value). The shelf you were on is restored; the
  year or genre you were inside is not.
- An ID3v1 trailer that writes genre byte 0 means "Blues" by the spec, and
  is taken as such. A tagger that zero-fills the trailer instead of writing
  255 will therefore shelve those files under Blues.
- The chip is mutated in place, never rebuilt, while it stays on screen.
  A press must not delete the object the touch is tracking, or the
  release that ends the hold never arrives and the mode hangs open — the
  same hazard `render()` already defers its screen deletion for.
- A swipe recognized from a finger that went down on the caption chip is
  ignored (`GestureEvent` now carries its start point). At a stack root a
  left-to-right swipe switches to the Files tab, and the chip is wide
  enough that a sloppy tap on it can drift the 40px that counts as a
  swipe -- so tapping it navigated to Files instead (found on the device
  2026-09-17). Same rule as the shuttle-held guard in `loop()`: a finger
  on a control is not a swipe.
- `kListTopY` did not move: the caption chip had to fit the existing 22 px
  header band rather than push the list down on a round screen where
  vertical space near the edges is already the scarcest thing there is.
