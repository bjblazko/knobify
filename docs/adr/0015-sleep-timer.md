# 0015: Sleep timer

## Status

Accepted — 2026-09-15. Amends ADR 0010 (Home tile layout).

## Context

README's backlog had "a sleep timer (auto-stop playback after a set time)".
It should be global, not a Now Playing feature: at bedtime the device as a
whole should go quiet and stop using the battery. The firmware can't cut
power — only the hardware switch can (ADR 0005) — and the idle timeout
only darkens the display, leaving the ESP32 running.

## Decision

### Where: a Home tile

Home gets a third tile, **Sleep** (Material `bedtime`, U+EF44). While the
timer runs its label shows the time left ("27 min"), so it can be seen
without opening anything.

Rejected:

- **A Settings row.** Settings configures the device; a sleep timer is
  something you do tonight. One tap further away, and mixed in with
  maintenance rows.
- **A fifth circle in the Now Playing options panel.** Ties a global
  function to music, and the panel's row of four has no room.

### The Sleep screen

Laid out like Brightness: glyph, value ("30 min" / "Off"), "Turn to set",
an accent ring at the bezel. The knob steps through **Off · 15 · 30 · 45 ·
60 · 90 · 120** minutes and each step takes effect at once, no confirm.
Steps go from the *time left*: at 27 min, one step up is 30, one down is
15. A preset within a minute of the time left counts as the current one, so
turning right after picking 30 gives 45. The ring counts down on a 0–120
min scale.

`power::SleepTimer` (`lib/power/`, host-tested) holds this logic. It is
never persisted: every boot starts with the timer off.

### When it runs out: fade, then deep sleep

1. **Fade** — the last 30 s of the timer lower the output to 0 along
   (time left)³, which falls about evenly in loudness (−18 dB halfway)
   ("Going to sleep"). The gain (0..4096) is applied per sample in the
   driver's `audio_process_i2s()` hook, not through the volume: its 22
   steps are too coarse. The volume setting is never touched, so the volume
   after waking is the one from before.

   First version, same day: 10 s, linear, through the volume steps. On the
   device it didn't sound like a fade at all — nearly unchanged, then a
   cut in the last seconds.
2. **Still awake?** A touch during the fade, or a turn while the display
   is on, cancels: volume back, timer off, "Sleep timer off". That input
   does nothing else. Ignored while locked (a pocket).
3. **Deep sleep** — pause, save the resume record now
   (`ResumeScheduler::saveNow()`), flush pending volume/brightness saves,
   backlight 0, panel sleep-in, arm the CST816 to interrupt on touch, then
   `esp_deep_sleep_start()` with ext0 wake on its INT pin (GPIO 9, low).

Waking is a reboot. It is not treated as a crash, so the resume record
restores the screen and track, paused (ADR 0012).

Only touch wakes it. The encoder pins could too, but a nudge in a pocket
would then reboot the device — the same reason a dark display needs a
quarter turn (ADR 0005).

Rejected: **pause and screen off only.** Simpler and no reboot, but the
ESP32 and audio codec keep draining the battery all night.

### Home layout: one row of three

The 2×2 grid of 112 px tiles (ADR 0010) didn't fit a third tile: the
second row ran into the 88 px mini-bar zone and a centered third label sat
behind the bezel. Tiles are now 84 px circles in one row, centers 106 px
apart (22 px gaps), at y=96 — unchanged whether the mini-bar shows.

## Consequences

- Waking takes a boot (a few seconds) rather than an instant.
- A fourth destination needs another layout decision. *(Made 2026-09-16 in
  ADR 0018: the row becomes a carousel.)*
- The CST816 wake settings come from its datasheet. Confirmed on the
  device 2026-09-15: fade, cancel, deep sleep, wake by touch and resume
  all work. Build with `-DKNOBIFY_SLEEP_DEBUG` for a 1-minute preset to
  try it again.
