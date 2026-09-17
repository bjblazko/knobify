"""Patches the pinned ESP32-audioI2S so M4A seeks land where they were asked to.

ESP32-audioI2S 2.3.0 declares and defines Audio::seek_m4a_stsz() but never
calls it (grep the library: only Audio.h's declaration and Audio.cpp's
definition). That function is the only writer of m_stsz_position, and
m4a_correctResumeFilePos() -- which every M4A seek and every M4A resume
offset passes through, via m_resumeFilePos in Audio::loop() -- opens with

    if(!m_stsz_position) return m_audioDataStart; // guard

So without the call, the guard always fires and every M4A seek is silently
rewritten to the first byte of audio: shuttling an M4A restarted the track on
every cue cycle while knobify's wall-clock readout kept counting, and resuming
an M4A from a bookmark always came back at 0:00. MP3 (syncword rescan) and WAV
(4-byte align) take other branches and were never affected.

This inserts the missing call at the end of read_M4A_Header(), where the
header is parsed and m_audioDataStart is known. See
docs/adr/0020-patching-esp32-audioi2s-m4a-seek.md for why this is a build-time
patch rather than a vendored copy of the library.

Wired in from platformio.ini as `extra_scripts = pre:scripts/patch-audioi2s.py`.
Idempotent (re-run on every build) and fails the build loudly if the anchor it
expects is gone, so a dependency change can never quietly drop the fix.
"""

import os
import sys

Import("env")  # noqa: F821 -- SCons injects this.

MARKER = "knobify patch:"

# Matched verbatim, including indentation. If ESP32-audioI2S ever changes these
# two lines the patch stops applying, and that must be noisy rather than silent.
ANCHOR = (
    "    if(m_controlCounter == M4A_AMRDY){ // almost ready\n"
    "        m_audioDataStart = headerSize;\n"
)

# seek_m4a_stsz() walks the atom tree with audiofile.seek() and leaves the
# shared file pointer at 0, which would make the decoder read the header back
# as audio -- hence the save/restore around it. Local files only: it reads the
# file directly, which a stream can't do (and knobify only plays from SD).
PATCH = """\
        // knobify patch: upstream 2.3.0 never calls seek_m4a_stsz(), so
        // m_stsz_position stays 0 and m4a_correctResumeFilePos()'s guard
        // sends every M4A seek back to m_audioDataStart. This restores the
        // behaviour the library intended. seek_m4a_stsz() moves the shared
        // file pointer and leaves it at 0, so save and restore it.
        if(getDatamode() == AUDIO_LOCALFILE){
            uint32_t knobify_resumePos = audiofile.position();
            seek_m4a_stsz();
            audiofile.seek(knobify_resumePos);
        }
"""


def fail(message):
    sys.stderr.write("\npatch-audioi2s.py: %s\n\n" % message)
    env.Exit(1)  # noqa: F821


def main():
    source = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"),  # noqa: F821
        env.subst("$PIOENV"),  # noqa: F821
        "ESP32-audioI2S",
        "src",
        "Audio.cpp",
    )
    if not os.path.isfile(source):
        fail(
            "%s not found. The library should be installed before the build "
            "runs; try `pio pkg install -e %s` and build again."
            % (source, env.subst("$PIOENV"))  # noqa: F821
        )

    with open(source, "r", encoding="utf-8", errors="surrogateescape") as handle:
        text = handle.read()

    if MARKER in text:
        return  # Already patched -- extra scripts run on every build.

    if text.count(ANCHOR) != 1:
        fail(
            "the M4A_AMRDY anchor was not found exactly once in %s, so the "
            "missing seek_m4a_stsz() call could not be inserted. M4A seeking "
            "and M4A resume would silently jump to the start of the track. "
            "Re-check the library against "
            "docs/adr/0020-patching-esp32-audioi2s-m4a-seek.md." % source
        )

    with open(source, "w", encoding="utf-8", errors="surrogateescape") as handle:
        handle.write(text.replace(ANCHOR, ANCHOR + PATCH))

    print("patch-audioi2s.py: added the missing seek_m4a_stsz() call to %s" % source)


main()
