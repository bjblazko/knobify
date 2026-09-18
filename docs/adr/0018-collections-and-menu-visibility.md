# 0018: Collections, the Home carousel, and a configurable main menu

## Status

Accepted — 2026-09-16. Generalises the music player of ADR 0004 into a
collection-parameterised audio player, and supersedes ADR 0010's Home tile
layout (already once amended by ADR 0015).

## Context

knobify had exactly one library: a `LibraryIndex` scanned from `/Music`,
cached in one `/knobify/library.idx`, behind one Home tile. The user also
listens to Hörbücher and Hörspiele.

Those are not music. Putting them in the same tag-based Artist → Album →
Track index buries them — a radio play showed up as an "artist" folder —
and they want different behaviour: no shuffle, name order rather than
tag/chronological order, and resuming where a title was left.

The obvious cheap answer, a second copy of the music screens, would have
meant maintaining the same browse/playback code three times.

## Decision

### A collection is the parameter, not a mode

`collection::CollectionProfile` (`lib/collection/`) is a row of plain data:
label, SD root, index cache path, whether its lists lead with a Shuffle
row, whether it resumes within a title, and its sort order. Music,
Audiobooks and Radio Plays are three rows of one table.

Everything below the UI was already root-agnostic — `LibraryScanner`,
`IndexCache`, `PlaylistBuilder`, `FolderBrowser`, `CoverArtCache` and
`PlayQueue` all work on a `LibraryIndex` or on plain paths — so the change
is concentrated where the single-root assumption actually lived:
`src/main.cpp`'s globals, and the UI/navigation/resume layer's single
injected `LibraryIndex &`.

- Roots are `/Music`, `/Audiobooks`, `/RadioPlays`. Each has its own cache
  (`library.idx`, `audiobooks.idx`, `radioplays.idx`). Music keeps the
  historical path, so updating does not invalidate an existing card.
- Covers stay in one `/knobify/covers`: `CoverArtCache::cacheFileNameFor()`
  hashes the full album folder path, so roots cannot collide.
- `library::LibraryRescanner` is replaced by `collection::CollectionSet`,
  which answers both "this collection's index" and "rescan it". The UI
  needed one of several indexes as well as a rescan trigger; that is one
  question, not two.

### Navigation carries the collection

`ScreenParams` gains a `CollectionId`. Every push copies it, so a browse
stack stays inside one collection and a screen's ids always mean something
in the index it names. `TabController` keeps one Library/Files stack pair
*per* collection, so leaving Audiobooks halfway into a series and coming
back lands where you left. A swipe still switches Library/Files, and never
crosses collections: it means "the other view of what I am browsing", not
"a different shelf".

The Files tab now roots at the collection's own folder rather than the
card's root. Browsing the whole card raw is no longer reachable — it was
the same view three times over otherwise, and the caption could not have
said which collection you were in.

### Home becomes a carousel

Five destinations do not fit any row on a 360 px round display. ADR 0015's
row of three 84 px tiles already spanned x=32..328.

Home now shows the selected destination as an 84 px tile at y=96 — ADR
0015's geometry, unchanged — with its two neighbours at 56 px and half
opacity 118 px either side, and a row of dots above at y=62, one per
destination. The knob rotates; a tap on the centre opens, a tap on a
neighbour rotates it in rather than opening something the user cannot yet
fully see. Wrapping in both directions, since the knob itself never stops.

ADR 0010 considered and rejected a dial carousel: "with few destinations,
seeing all of them is more understandable" (Rams #4). That was right for
two destinations and is wrong for five — the dots keep the count visible,
which is the part that mattered.

Only the centre tile is labelled; three labels at this spacing overlapped.
Two new glyphs were added to the icon fonts: `menu_book` (U+EA19) for
Audiobooks and `theater_comedy` (U+EA66) for Radio Plays. The 28 px font
gained the five menu glyphs, since the neighbour tiles draw at that size.

### The wordmark

Home carries a "knobify" wordmark: a small dial and the word, set in an
`ink` capsule with both drawn in `surface`. The dial is a disc with a
pointer notched out of it, set off vertical so it reads as a knob at a
setting rather than a full stop.

The pair was first drawn in ink directly on the surface, and at that size
it read as a bullet point in front of a word rather than as a mark (user,
2026-09-17). Inverting it into a capsule fixes that without adding a
colour or an effect: it becomes a name-badge on a front panel, which is
how a Braun device says what it is.

The letters are tracked out (3px at 16px, about 0.2em). At the default
spacing the word read as typed rather than set, which is not what a badge
on a front panel looks like (user, 2026-09-18). It is the one typographic
liberty taken anywhere in this UI, and only here, because the wordmark is
the only string on screen that is a name rather than information. The
capsule is measured from the text, so the tracking is a single constant
with nothing else to keep in step.

A red bullet was tried first and was wrong on the palette's own terms
(ux-guidelines §3 rule 2): every colour in this system means something, a
brand mark means nothing in that sense, and the red it borrowed is
`warning` — a flat battery. The mark is drawn in ink like the text it
belongs to, and earns its place through form instead. Its indicator is a
dot placed off-centre rather than a rotated bar: LVGL 8 only applies
`transform_angle` to images, so a rotated plain object renders upright.

Shuffle also leaves Now Playing's options panel for spoken word. The lists
had already dropped their Shuffle row, which left the toggle as the one
remaining way to shuffle an audiobook's chapters. It is judged by what is
*playing*, not by the screen behind the panel: a book can be running while
Music is being browsed, and those buttons act on the queue.

### The user decides what Home shows

Settings > Main menu lists the five destinations, each row ending in
"On"/"Off" as plain text (ux-guidelines §7). A user with no audiobooks, or
who never wants a sleep timer, hides them and the carousel shrinks.

`navigation::MenuVisibility` holds the rules as pure logic — a bit per
entry in one NVS byte (`menuVis`), plus two guards: Settings can never be
hidden (it is the only way back to this screen), and the last visible
entry cannot be hidden either. Both refusals say why rather than leaving
the row looking dead (Rams #4). Keeping this out of `ScreenManager` is what
makes it host-testable, per `docs/coding-guidelines.md`.

### Rescan picks a collection

Settings' "Rescan library" becomes "Rescan", opening a list of the three
collections plus "All". Rescanning all three when the user added one
audiobook would walk every root for nothing. Leaving a USB drive session
still rescans everything: the computer could have written anywhere.

### Resume

`MusicResumeSource`/`MusicSnapshot` become `PlaybackResumeSource`/
`PlaybackSnapshot` — the type was never about music.

There is one player, so there is still exactly one playback snapshot; it
gains a collection byte saying which shelf the queue came from. That byte
is *derived from the track's path* at capture time (`CollectionSet::
findByPath()`, unambiguous because roots never nest) rather than tracked
alongside playback, so nothing can forget to update it.

`NavigationSnapshot` saves only the active collection's stacks plus its id.
Three collections' worth of full paths would not fit `ResumeCodec::
kMaxEncodedSize` (3072 bytes), and coming back to the shelf you left is
what actually matters.

That changes the navigation section's layout mid-record, so
`ResumeCodec::kVersion` is 2. v1 blobs are rejected, which every caller
already treats as "no record" — an updated device starts on Home once.

### Spoken word remembers where each title was

Session resume (above) restores the one thing that was playing when the
power went. That is not enough for an audiobook: leaving it to play music
and coming back should return to the spot, and there may be several books
on the go.

`resume::Bookmarks` keeps one position per *title* — the folder holding
its parts, so the key is the book, not the chapter — for collections whose
profile sets `resumesWithinTitle`. It is bounded at 12 entries, most
recently used first: a device used for years should not grow an unbounded
NVS blob, and the titles you are part-way through are the recent ones.
Encoding drops the oldest entries rather than refusing to save, so one
very long path cannot make the whole set unwritable.

`resume::BookmarkKeeper` samples the position every 2 s and writes at most
every 30 s, and only when something changed — the same "there is no
shutdown signal" shape as `ResumeScheduler`, but much less often, because
a bookmark that is a few seconds stale costs nothing and NVS writes are
what to be stingy with. Unlike the session record, bookmarks survive a
crash: a position in an audiobook cannot be what caused one.

**A Continue row, not a silent jump.** A spoken-word Tracks list leads
with "Continue" in the slot Shuffle occupies for music. Tapping a part
still plays that part from its start — an explicit choice is never
overridden by a remembered position, which is the surprising behaviour
that auto-resuming whatever you tapped would have. The row only appears
when the remembered part is still in the album, so a rescan that moved
files cannot offer to resume nothing.

Not done: nothing forgets a title when it plays out, so Continue on a
finished book points at the end of its last part. `Bookmarks::forget()`
exists for when that is worth wiring to a real "finished" signal.

## Consequences

- Adding a fourth collection is one row in `kCollections` plus one in
  `kMenuEntries`; the carousel and the visibility byte already scale (the
  latter to 8 entries, static_assert'd).
- `kMenuEntries` and `CollectionId` are both append-only now: their order
  is the visibility byte's bit order and a stored resume value.
- The carousel is the one piece of this that a screenshot cannot validate
  — neighbour tiles and dots sit near the bezel. Confirm on the device
  (ux-guidelines §7).
- Untagged spoken-word files rely on `<root>/<Artist>/<Album>/track`, same
  as music: the folder above the file is the "album", the one above that
  the "artist" (a series).
