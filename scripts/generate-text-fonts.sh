#!/usr/bin/env bash
# Generates lib/ui-widgets/TextFont14/16/20/28.c -- replacements for
# LVGL's built-in lv_font_montserrat_14/16/20/28 that add Latin-1
# Supplement and Latin Extended-A coverage (umlauts, accents,
# typographic punctuation) so European music tags stop rendering as
# missing-glyph boxes.
#
# Reuses the same source files LVGL's own built-in fonts were generated
# from (scripts/built_in_font/ under the lvgl lib dep), so glyph shapes
# and metrics match the built-ins they replace. The FontAwesome
# codepoint list is copied VERBATIM from each built-in font's own
# "* Opts:" header comment -- that list is what makes every LV_SYMBOL_*
# icon used elsewhere in the app keep working.
#
# --no-compress is NON-NEGOTIABLE: this project's lv_conf.h sets
# LV_USE_FONT_COMPRESSED 0, and a compressed font draws zero pixels
# while still sizing labels correctly -- a documented debugging trap,
# see AGENTS.md and lib/ui-widgets/IconFont.c's header comment.
#
# @latest is required: older cached lv_font_conv versions lack
# --lv-font-name (AGENTS.md), and without it the generated font symbol
# won't match TextFont.h's LV_FONT_DECLARE.
#
# Metrics pinning: lv_font_conv computes .line_height/.base_line from
# the actual ascent/descent of every glyph it rasterizes. The built-in
# Montserrat fonts only cover ASCII + the FontAwesome icon range, so
# their line metrics come from plain Latin letters. Adding Latin-1
# Supplement/Latin Extended-A pulls in 14 glyphs (out of 224 added) that
# are genuinely taller/deeper than the rest -- Scandinavian ring-above
# letters (Å, Ů), an accent sitting on an ascender (ĥ, ĺ), and the
# Latvian/Cornish comma-below family (Ķķ, Ļļ, Ņņ, Ŗŗ, Ģ, ¸) -- which
# inflates lv_font_conv's computed line_height/base_line for the whole
# font, even though every other added accented glyph (Ä Ö Ü É È À Ñ Ç
# Æ ß etc.) has exactly the built-in's own ascent/descent. None of the
# 14 outlier glyphs occur in this device's music library, so after
# generating each file this script pins .line_height/.base_line back to
# the built-in lv_font_montserrat_$SIZE values below -- keeping every
# label's size/position identical to before this font replaced the
# built-in -- at the cost of clipping those 14 glyphs by 1-4px at the
# very tip. See the struct comment each pinned file gets (inserted
# below) for the exact glyph list and per-size deltas.
set -euo pipefail
cd "$(dirname "$0")/.."

LVGL_FONTS="$(pwd)/.pio/libdeps/esp32-s3/lvgl/scripts/built_in_font"
MONTSERRAT="$LVGL_FONTS/Montserrat-Medium.ttf"
FONTAWESOME="$LVGL_FONTS/FontAwesome5-Solid+Brands+Regular.woff"

if [[ ! -f "$MONTSERRAT" || ! -f "$FONTAWESOME" ]]; then
  echo "error: source fonts not found under $LVGL_FONTS" >&2
  echo "  expected: $MONTSERRAT" >&2
  echo "  expected: $FONTAWESOME" >&2
  echo "run 'pio pkg install' to fetch the lvgl library dependency first." >&2
  exit 1
fi

# Verbatim from lv_font_montserrat_*.c's own "* Opts:" line (all four
# sizes share the same FontAwesome -r list) -- keeps every LV_SYMBOL_*
# icon rendering after these fonts replace the built-ins.
FONTAWESOME_RANGE='61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650'

# 0x20-0x7F: Basic Latin (matches the built-ins' own ASCII range).
# 0xA0-0xFF: Latin-1 Supplement (umlauts, accents, degree sign, etc.).
# 0x100-0x17F: Latin Extended-A (e.g. Polish/Czech/Croatian letters).
# 0x2013,0x2014,0x2018-0x201D,0x2022,0x2026: en/em dash, curly quotes,
# bullet, ellipsis -- typographic punctuation that shows up in tag text.
TEXT_RANGE='0x20-0x7F,0xA0-0xFF,0x100-0x17F,0x2013,0x2014,0x2018-0x201D,0x2022,0x2026'

# Built-in lv_font_montserrat_$SIZE.c's own .line_height/.base_line --
# what these fonts get pinned back to. Ring-above/comma-below deltas
# (px lv_font_conv's computed line_height grew by at this size, from
# those 14 outlier glyphs) are recorded here purely to stamp into each
# file's struct comment. (Plain parallel-case lookup, not an
# associative array: this repo's /usr/bin/env bash resolves to macOS's
# stock bash 3.2, which has no `declare -A`.)
metrics_for_size() {
  case "$1" in
    14) echo "16 3 2 1" ;;
    16) echo "18 3 2 2" ;;
    20) echo "22 4 2 2" ;;
    28) echo "30 5 4 3" ;;
    *) echo "error: no pinned metrics recorded for size $1" >&2; exit 1 ;;
  esac
}

patch_metrics() {
  local file="$1" size="$2"
  local target_lh target_bl ring_delta comma_delta
  read -r target_lh target_bl ring_delta comma_delta <<<"$(metrics_for_size "$size")"
  python3 - "$file" "$size" "$target_lh" "$target_bl" \
    "$ring_delta" "$comma_delta" <<'PYEOF'
import re
import sys

path, size, target_lh, target_bl, ring_delta, comma_delta = sys.argv[1:7]

with open(path, "r", encoding="utf-8") as f:
    text = f.read()

lh_re = re.compile(r"^(\s*)\.line_height = (\d+),(.*)$", re.MULTILINE)
bl_re = re.compile(r"^(\s*)\.base_line = (\d+),(.*)$", re.MULTILINE)

# Idempotency: strip a struct comment/header note left over from a
# previous run of this script before computing/reinserting them, so
# re-running always starts from lv_font_conv's raw output.
text = re.sub(
    r"[ \t]*// Hand-pinned to lv_font_montserrat_.*?\n(?:[ \t]*//.*\n)*",
    "",
    text,
)
text = re.sub(
    r"( \* Opts:.*\n) \*\n \* This font REPLACES.*?\n(?: \*.*\n)*?(?= \*{5,}/)",
    r"\1",
    text,
    count=1,
    flags=re.DOTALL,
)

lh_match = lh_re.search(text)
bl_match = bl_re.search(text)
if not lh_match or not bl_match:
    sys.exit(f"error: could not find .line_height/.base_line in {path}")

gen_lh = lh_match.group(2)
gen_bl = bl_match.group(2)

struct_comment = (
    "    // Hand-pinned to lv_font_montserrat_{size}'s own line_height/base_line\n"
    "    // ({target_lh}/{target_bl}) instead of lv_font_conv's own computed\n"
    "    // {gen_lh}/{gen_bl}, so this font -- which REPLACES that built-in --\n"
    "    // causes no layout shift anywhere in the app (LVGL 8.3 sizes/places a\n"
    "    // label using only its font's line metrics). The added Latin-1\n"
    "    // Supplement/Latin Extended-A glyphs are what inflated the computed\n"
    "    // value: every Western/Central European accented letter (A umlaut,\n"
    "    // O umlaut, U umlaut, e acute, n tilde, c cedilla, ae/sharp-s, etc.)\n"
    "    // has the exact same ascent/descent as plain Montserrat and fits this\n"
    "    // line box perfectly. Only two small groups don't: the ring-above\n"
    "    // letters U+00C5 \"A-ring\" and U+016E \"U-ring\", and an accent sitting\n"
    "    // on an ascender, U+0125 \"h-circumflex\" and U+013A \"l-acute\" (all\n"
    "    // clipped ~{ring_delta}px at the very top against this pinned box); and the\n"
    "    // comma-below family U+0136/0137, U+013B/013C, U+0145/0146,\n"
    "    // U+0156/0157 plus U+0122 and U+00B8 (clipped ~{comma_delta}px at the very\n"
    "    // bottom). None of these occur in this device's music library.\n"
    "    // Regenerating via scripts/generate-text-fonts.sh reapplies this pin\n"
    "    // automatically -- keep it in sync with the other TextFont*.c sizes\n"
    "    // and with lv_font_montserrat_{size}.c if those values ever change.\n"
).format(
    size=size,
    target_lh=target_lh,
    target_bl=target_bl,
    gen_lh=gen_lh,
    gen_bl=gen_bl,
    ring_delta=ring_delta,
    comma_delta=comma_delta,
)

text = lh_re.sub(
    lambda m: struct_comment + f"{m.group(1)}.line_height = {target_lh},{m.group(3)}",
    text,
    count=1,
)
text = bl_re.sub(
    lambda m: f"{m.group(1)}.base_line = {target_bl},{m.group(3)}",
    text,
    count=1,
)

# Header rationale, inserted right after lv_font_conv's own "* Opts:"
# line (kept intact) and before the closing "*/" of that same comment
# block.
header_note = (
    " *\n"
    " * This font REPLACES LVGL's built-in lv_font_montserrat_{size} (which\n"
    " * lv_conf.h now disables) so that European music tag text -- umlauts,\n"
    " * accents, and a handful of typographic punctuation marks -- stops\n"
    " * rendering as missing-glyph boxes. Generated from the exact same\n"
    " * source files LVGL's own built-in fonts were generated from\n"
    " * (lvgl/scripts/built_in_font/, bundled with the LVGL 8.3.x library\n"
    " * dependency): Montserrat-Medium.ttf (SIL Open Font License 1.1) and\n"
    " * FontAwesome5-Solid+Brands+Regular.woff (Font Awesome 5 Free, CC BY\n"
    " * 4.0 icons / SIL OFL 1.1 font) -- see THIRD-PARTY.md.\n"
    " *\n"
    " * Ranges: 0x20-0x7F (Basic Latin, matches the built-in's own ASCII\n"
    " * coverage), 0xA0-0xFF (Latin-1 Supplement) and 0x100-0x17F (Latin\n"
    " * Extended-A) for European tag text, plus en/em dash, curly quotes,\n"
    " * bullet and ellipsis (typographic punctuation seen in tags). The\n"
    " * FontAwesome -r codepoint list is copied VERBATIM from this size's\n"
    " * built-in lv_font_montserrat_{size}.c \"* Opts:\" line, unchanged --\n"
    " * that is what keeps every LV_SYMBOL_* icon used elsewhere in the app\n"
    " * rendering correctly.\n"
    " *\n"
    " * --no-compress is required (LV_USE_FONT_COMPRESSED is 0 in\n"
    " * lv_conf.h) -- a compressed font sizes labels correctly but draws\n"
    " * zero pixels, a documented debugging trap, see AGENTS.md and\n"
    " * lib/ui-widgets/IconFont.c's header comment. Keep it if\n"
    " * regenerating/extending.\n"
    " *\n"
    " * Generated (and .line_height/.base_line pinned -- see the struct\n"
    " * comment below) by scripts/generate-text-fonts.sh. Regenerate with\n"
    " * that script, never by hand.\n"
).format(size=size)

opts_re = re.compile(r"(^ \* Opts:.*\n)", re.MULTILINE)
if not opts_re.search(text):
    sys.exit(f"error: could not find '* Opts:' line in {path}")
text = opts_re.sub(lambda m: m.group(1) + header_note, text, count=1)

with open(path, "w", encoding="utf-8") as f:
    f.write(text)

print(f"  {path}: pinned line_height {gen_lh} -> {target_lh}, base_line {gen_bl} -> {target_bl}; header note inserted")
PYEOF
}

for SIZE in 14 16 20 28; do
  echo "Generating TextFont${SIZE}.c ..."
  OUT="lib/ui-widgets/TextFont${SIZE}.c"
  npx --yes lv_font_conv@latest \
    --no-compress --no-prefilter --bpp 4 --size "$SIZE" \
    --font "$MONTSERRAT" \
      -r "$TEXT_RANGE" \
    --font "$FONTAWESOME" \
      -r "$FONTAWESOME_RANGE" \
    --format lvgl --force-fast-kern-format \
    --lv-include lvgl.h --lv-font-name "knobify_text_font_${SIZE}" \
    -o "$OUT"
  patch_metrics "$OUT" "$SIZE"
done

echo "Done. Generated lib/ui-widgets/TextFont14.c, TextFont16.c, TextFont20.c, TextFont28.c"
