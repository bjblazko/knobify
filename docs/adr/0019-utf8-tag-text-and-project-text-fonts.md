# 0019: UTF-8 tag text and project-generated text fonts

## Status

Accepted — 2026-09-17. Verified on the physical device the same day:
umlauts and accents render as written in the browse lists and on Now
Playing, and the `LV_SYMBOL_*`/`IconFont` glyphs still draw, which is
what proves the FontAwesome range survived the font regeneration.

## Context

The music library on the card is German and European: "Hör gar nicht
hin", "Das weiß ich", "Le Voyage de Pénélope", "El Mañana",
"Discothèque", "Ænima", "Don't Give Up" with a curly apostrophe. On the
device none of those rendered as written. Measuring a 3174-file library
turned up two independent defects, both of which had to be fixed before
a single umlaut could reach the screen:

**Tag decoding threw the characters away before any font was
consulted.** Every non-ASCII text frame checked in the library is ID3v2
encoding 1 (UTF-16 with a BOM), and `Id3v2Parser` kept only code units
below 0x80 -- "Björk" became "Bjrk", not a missing-glyph box. Files using
encoding 0 (ISO-8859-1/Latin-1) fared differently but no better: their
raw bytes were passed straight through and stored as a `std::string`
that was not valid UTF-8, which is whatever code downstream (LVGL's
label rendering, `IndexCache`'s serialization) happens to do with
invalid UTF-8 -- undefined by contract even where it looked harmless.

**No font carried the glyphs either.** LVGL's built-in Montserrat fonts
(`LV_FONT_MONTSERRAT_14/16/20/28`) cover only 0x20-0x7F plus
`LV_SYMBOL_*` (ADR 0008), so even correctly decoded text would have
rendered umlauts and accents as missing-glyph boxes, same as the U+00B7
middle dot ADR 0008 worked around by substituting `-` for `·`.

Fixing either alone would not have fixed what the user actually sees:
correct bytes into a font without the glyphs still show boxes; the right
glyphs fed garbled bytes still show garbage.

## Decision

### Convert to UTF-8 at the parser boundary

`lib/library/Utf8.h` (`knobify::library::utf8`) adds `fromLatin1()`,
`fromUtf16()` (handling both byte orders and surrogate pairs, dropping
unpaired surrogates), `isValidUtf8()`, and `truncate()` (UTF-8-boundary-
aware, for `Mp4Parser`'s 512-byte text cap, which used to be able to cut
a sequence in half). `Id3v2Parser`'s
`decodeText()` now dispatches on the frame's own encoding byte (0:
Latin-1, 1: UTF-16+BOM, 2: UTF-16BE, 3: UTF-8) into these instead of
truncating to ASCII.

A tag can still lie about its own encoding -- a file marked UTF-8
(encoding 3) that is actually Latin-1 bytes, the most common way real
files misbehave. Rather than adding per-format guesswork inside every
parser, `Utf8.h` adds one `repair()` fallback: if a string already
validates as UTF-8 it is returned untouched, otherwise its bytes are
re-read as Latin-1 and re-emitted as UTF-8, which is always well-formed
LVGL input. It is applied once, at `TagReader::read()`'s single choke
point where every format's parser result already funnels through --
`Id3v2Parser`, and any future format's parser, get the fallback for free
without each needing its own encoding-mismatch defense.

### Replace, don't fall back: full Montserrat fonts with a wider range

`scripts/generate-text-fonts.sh` regenerates `lib/ui-widgets/
TextFont14/16/20/28.c` (`knobify_text_font_14/16/20/28`) from the same
`Montserrat-Medium.ttf` and `FontAwesome5-Solid+Brands+Regular.woff`
LVGL's own built-ins use (found under `.pio/libdeps/esp32-s3/lvgl/
scripts/built_in_font/`), with the FontAwesome codepoint range copied
verbatim from the built-ins' own generation comment so every
`LV_SYMBOL_*` icon used elsewhere keeps working. The text range grows
from 0x20-0x7F to 0x20-0x7F, 0xA0-0xFF (Latin-1 Supplement), 0x100-0x17F
(Latin Extended-A), plus en/em dash, curly quotes, bullet and ellipsis --
covering the accented Latin scripts the library actually contains, not
just German. `LV_FONT_MONTSERRAT_14/16/20/28` are switched off in
`include/lv_conf.h`; `LV_FONT_CUSTOM_DECLARE` declares the four
replacements and `LV_FONT_DEFAULT` points at `knobify_text_font_16`.

The alternative -- a small fallback font carrying only the extra
glyphs, `.fallback`-chained onto the existing Montserrat build, the same
shape already used for `IconFont16`'s icon glyphs -- was rejected. That
shape only works because `IconFont16` is deliberately sized to match its
fallback's metrics by hand (documented at length in that file's header):
LVGL 8.3 sizes and vertically places every glyph in a label using only
the label's *primary* font's line metrics, never the fallback's, so a
label mixing primary and fallback glyphs needs both fonts' metrics kept
in sync by hand, forever, as either changes. A full replacement sidesteps
that trap by construction -- one font, one set of metrics and kerning,
generated together -- and duplicates no glyph already in the ASCII range
the built-ins covered.

### Pin the line metrics to the built-ins' values

`lv_font_conv` derives `line_height` and `base_line` from the glyphs it
is actually asked to rasterize, so widening the range made all four
fonts taller than the built-ins they replace (14 px: 19/4 instead of
16/3; 16 px: 22/5 instead of 18/3; 20 px: 26/6 instead of 22/4; 28 px:
37/8 instead of 30/5). Shipping that would have moved every label in the
app.

Probing each codepoint in 0xA0-0x17F individually against the same TTF
showed the cause is 14 glyphs out of 224. Every accented letter that
actually occurs in Western and Central European text -- Ä Ö Ü É È À Ñ Ç
Æ ß Á Â Ã Ê Î Ô Õ Ú Û Ý Ā Ć Č Ď Ę Ğ Ł Ń Ő Ř Š Ť Ū Ű Ž and the rest --
has *exactly* the built-in's ascent and descent at every one of the four
sizes, and fits the existing line box pixel-perfectly. The outliers are
ring-above (Å, Ů), an accent stacked on an ascender (ĥ, ĺ), and the
comma-below family (Ķķ Ļļ Ņņ Ŗŗ Ģ ¸).

So `scripts/generate-text-fonts.sh` pins the two metrics fields back to
the built-ins' values after generating each font, and records in the
file what was overridden and why. Those 14 glyphs lose 1-4 px at the
extreme tip and stay legible; nothing else moves. The alternative --
dropping the outliers from the range -- was rejected because a clipped Å
still reads as an Å, while a missing one renders as a box.

### Consequences of the font swap

- `IconFont16`'s `.fallback` now points at `knobify_text_font_14`
  instead of `lv_font_montserrat_14`; its metrics-matching comment
  (line_height/base_line hand-copied from the fallback) is unchanged in
  substance, just repointed.
- +104 KB of flash for the four fonts' wider glyph tables
  (firmware.bin 1,384,608 -> 1,490,832 bytes; 22.7% of the 6.25 MB
  app partition).
- The U+00B7 middle dot (Now Playing's artist/album separator) and the
  U+00D7 "×" (jog-shuttle speed pill) are back; ADR 0008's and ADR
  0013's ASCII substitutions are reverted.
- `IndexCache::kFormatVersion` is bumped to 4: a device's existing cache
  holds text mangled by the old UTF-16-truncating parser, and serving it
  after this change would keep showing those strings indefinitely. Since
  nothing scans at boot any more (ADR 0018 moved the 17-19 s walk behind
  Settings > Rescan), a v3 cache now decodes as "no cache yet" and each
  collection comes up **empty until the user runs Settings > Rescan**.
  That is the intended one-off upgrade cost -- an empty shelf that one
  rescan fills correctly, rather than a full shelf of wrong text -- but
  it is a visible one, and any future format bump pays it again.

### What is not fixed

`SD_MMC`/FATFS returns OEM-codepage bytes for non-ASCII *filenames* (as
opposed to tag text read from inside the file), which this change does
not touch -- fixing it means reconfiguring FATFS's codepage tables, a
different layer than tag decoding. This is moot for a fully tagged
library, where every screen shows tag text rather than a raw filename;
it only resurfaces for untagged spoken-word files, which already fall
back to filename/folder names (ADR 0018) and would show those names
lossily if they contain non-ASCII characters.

## Consequences

- Any tag format parser (ID3v2, MP4, Vorbis comments, RIFF INFO) must
  decode to UTF-8 before returning; `TagReader::read()`'s
  `repair()` call is a safety net, not a substitute for correct
  per-encoding decoding.
- Widening character coverage further (e.g. Cyrillic, Greek) means
  editing `TEXT_RANGE` in `scripts/generate-text-fonts.sh` and rerunning
  it -- see AGENTS.md.
- `docs/adr/0008-braun-design-system-and-screen-redesign.md`'s
  "Built-in Montserrat is ASCII-only" note and
  `docs/adr/0013-jog-shuttle.md`'s ASCII-`x` note are both superseded by
  this ADR.
