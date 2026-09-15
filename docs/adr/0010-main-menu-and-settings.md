# 0010: Main menu, settings, and display brightness

## Status

Accepted — 2026-09-15. Fills the Home/menu slot ADR 0004 left open above
the music roots, and moves the library rescan out of the Library header.

## Context

knobify booted straight into the Library, and README's backlog had a
settings screen waiting for "a home/main-menu screen to hang it off". The
menu needs to grow later (more destinations), the first real setting is
display brightness, and the device has only a touch screen and a
rotation-only knob — no button to click.

## Decision

### Home: a grid of large round tiles

Home shows one tile per destination — a 112px circle with a 48px icon and
a 16px label below — two per row, starting with **Music** and **Settings**.
The knob moves the selection, touch selects on press and opens on release.
Unselected tiles look like Secondary buttons (`surfaceAlt`, `ink` glyph);
the selected tile is `ink` with a `surface` glyph, like a selected list row
— selection, never action, so no accent. Home has no title, caption or
back button. The mini-bar shows while music plays. The selection is
remembered, so coming back selects the tile you left from.

Entries are one row in a table (`kMenuEntries`, `ScreenManagerMenu.cpp`).
Up to four fit as a 2×2 grid inside the circle; anything beyond that needs
a new layout decision, verified on the device.

A dial carousel (one big item, the knob rotating the next one in) was
considered: it fits the round knob and scales to any count, but shows only
one option clearly at a time. With few destinations, seeing all of them is
more understandable (Rams #4).

### Navigation: a third stack

`TabController` gains `Tab::Menu`, a stack rooted at `Home`, and starts
there (boot opens the menu). Settings and Brightness are pushed onto it;
Now Playing opened from Home's mini-bar is too. Music re-enters whichever
music tab was used last, stacks intact. A swipe still pops, or switches
Library/Files at a music root; on Home it does nothing. The back button
pops, or at a music root goes to the menu — so it is now shown on the
Library/Files roots too.

### Rescan moves into Settings

That back button needs the top-center slot the rescan button used. Two
off-center buttons near the bezel were rejected (the single most repeated
hardware bug, ux-guidelines §7), so rescan became a "Rescan library" row in
Settings: a maintenance action lives with the other system functions.

### Brightness

Settings is a normal list; the Brightness row ends in its value ("60%").
Its screen shows a light icon, the percentage, a "Turn to adjust" hint (the
screen has nothing to press) and an accent edge ring, like volume. The
backlight follows the knob immediately; no confirm step.

`BrightnessSetting` (`lib/power/`) stores a level 1–10 (10%–100%), one
detent per step, persisted in NVS (`brightness`) after 1 s without changes,
like volume. Levels map to PWM duty on a gamma-2.2 curve from a floor of
10/255 to 255, so steps look even. The lowest level is never dark: a black
screen would be mistaken for the display being off (Rams #6). Default is
full brightness, matching the previous fixed value. The idle timeout still
switches the backlight fully off and restores the chosen level on wake.

## Consequences

- Music is one tap further away after boot.
- `ScreenManager` grew a second translation unit (`ScreenManagerMenu.cpp`)
  and a shared `ScreenHelpers.h`.
- The duty floor and tile placement need confirmation on the physical
  device.
