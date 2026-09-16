# 0012: Resume where you left off

## Status

Accepted — 2026-09-15. Replaces the README backlog item "resuming playback
position after a restart or power loss".

*Amended 2026-09-16 by [ADR 0018](0018-collections-and-menu-visibility.md):
`MusicSnapshot` is `PlaybackSnapshot` and carries the collection its queue
came from; the navigation snapshot saves only the active collection's
stacks, plus that collection's id. `ResumeCodec::kVersion` is 2 and v1
records are dropped.*

## Context

Every boot started on Home with nothing loaded. The user wanted the device to
come back where it was: the same screen, the same track, roughly the same
position. It must not auto-play. It should also work for later sources
(podcasts, web radio, a video player).

knobify gets no shutdown signal. Power can go at any moment, including while
the state is being saved. A half-written or otherwise bad record must never stop
the device from booting; in that case it just starts on Home.

## Decision

### Save periodically, decide by value what changed

`ResumeScheduler` (`lib/resume/`) captures the state once a second from each
`ResumeSource` and compares it with the last saved record:

- Anything but the playback position changed (screen, tab, back-stack, track,
  shuffle): saved once it has been stable for **3 s**.
- Only the position changed (a track is playing): saved at most every **30 s**.
- Nothing changed (paused, idle): **no writes**.

Nothing in the UI has to mark state dirty, so new screens are covered
automatically. A power cut loses a few seconds of navigation or up to 30 s
of position. The user said precision to the second isn't needed.

### Storage: one NVS blob, checked on read

The record is one NVS blob (`resume` in the `knobify` namespace), next to
volume and the other settings.

- **Power-loss safety.** NVS writes a changed value to new entries and only
  then marks the old ones erased. After a cut the key holds the old or the new
  record, never a mix. NVS lives on internal flash, so it doesn't depend on the
  SD card either.
- **Checked on read.** The record carries its own header: `"KRES"`, a
  version, the payload length and a CRC-32. Decoding is bounds-checked and
  range-checked. Any failure means start on Home.
- **Validated against the present.** The CRC can pass while the record still
  names something that no longer exists (a rescan, a different SD card). Each
  source restores what still resolves and drops the rest (below).
- **Crash guard.** If the last reset was a panic or a watchdog, the record is
  deleted and the device starts on Home. A record that crashes the firmware
  can cost at most one boot, never a reboot loop.

Rejected: a file on the SD card. FAT isn't power-loss safe (a cut can leave a
truncated file or a damaged FAT), and the card is the flakiest peripheral on
this board (AGENTS.md).

Flash wear: a ~300-byte record is about 12 NVS entries. Saving every 30 s
through non-stop playback is ~2 900 writes a day, spread over the partition's
5 pages. That is far within the flash's erase endurance.

### Format: tagged sections

Payload = `u8 tag | u16 length | body` sections. Unknown tags are skipped, and
bytes after a body's known fields are ignored. A later source adds a section,
or fields at the end of one, without a version bump, and older firmware still
reads the rest.

| Tag | Section | Body |
| --- | --- | --- |
| 1 | Navigation | active tab, last music tab, each tab's back-stack |
| 2 | Music | scope, current track path, shuffle, file byte offset, elapsed s |

Tags are stored values: never reuse or renumber one. The same goes for
`playback::PlayScope` and `navigation::ScreenKind` values.

Library screens are saved by **name** (artist; artist + album title), not by
id. Ids are indices into the library index and change on a rescan.

### Sources

Each resumable part implements `ResumeSource` (`capture` / `restore`).
`ResumeScheduler` restores them in order at boot, after the library index is
loaded and before the first render.

- **`MusicResumeSource`** saves the current track path and the scope, not the
  queue. On restore it rebuilds the queue with `PlaylistBuilder`, the same way
  a tap does. The Files scope checks the file still exists. With shuffle on,
  the saved track stays current and the rest is reshuffled after it. The old
  shuffled order isn't kept.
- **`NavigationResumeSource`** rebuilds every tab's back-stack, so Back still
  leads where it did. A stack is cut at the first screen that no longer
  resolves or doesn't belong in that tab. Now Playing is kept only on top, and
  only if the music source restored a queue.

A podcast, radio or video player gets its own section tag and source.

### No auto-play: a cued track

`PlaybackStateMachine::cue()` loads the queue as **Paused**, with the saved
elapsed time frozen on screen, and makes no driver call. Boot stays silent and
does no audio file open. The first play calls
`PlaybackDriver::playFileAt(path, offset)`. That maps to ESP32-audioI2S's
`connecttoFS(fs, path, resumeFilePos)`, which moves the byte offset to a frame
boundary for MP3, FLAC, M4A and WAV itself. The offset comes from
`Audio::getFilePos()`. A byte offset was chosen over
`setAudioPlayPosition(sec)`, which only handles MP3/WAV and needs the header
already parsed. Skipping while cued plays the other track from its start.

## Consequences

- `ScreenManager`'s play scope moved into `PlaybackStateMachine`
  (`play(..., scope)`), so resume logic is host-testable without LVGL.
- NVS writes briefly pause flash-cache access on both cores. Volume already
  saved during playback without audible effect; watch for stutter at the 30 s
  saves on the device.
- Two albums of the same artist with the same title are indistinguishable by
  name; resuming a Tracks screen picks the first.
- The saved position is what was heard: the reader position minus what is
  still in the decoder's input buffer (as `Audio::stopSong()` computes it).
  It was first the reader position itself, a whole buffer ahead -- resume
  landed seconds late (up to ~17 s with the library's 300 KB buffer); fixed
  2026-09-15 and verified on the device. While playing it can still be up
  to 30 s behind from save spacing; a pause is saved within 3 s.
- Tests: `test/test_resume/` (codec truncation and bit-flip rejection,
  scheduler timing, restore after rescan and with vanished items, cued play).
