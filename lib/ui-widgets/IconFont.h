#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Custom 28px icon font holding just the glyphs this project needs
// (lock, lock_open, shuffle, repeat, repeat_one) -- see IconFont.c's header comment for the
// exact source/version/license, and ADR 0005 for why a custom font
// rather than LV_SYMBOL_* (LVGL's built-in symbol subset has no lock
// icon at all).
LV_FONT_DECLARE(knobify_icon_font_28);

// UTF-8 encodings of the two glyphs, for lv_label_set_text() calls --
// matches how LV_SYMBOL_* constants are used elsewhere in this codebase.
#define KNOBIFY_ICON_LOCK_OPEN "\xEE\xA2\x98"  // U+E898
#define KNOBIFY_ICON_LOCK "\xEE\xA2\x99"       // U+E899
// Now Playing's shuffle/repeat toggles (ADR 0011), in the options panel
// since ADR 0014.
#define KNOBIFY_ICON_SHUFFLE "\xEE\x81\x83"     // U+E043
#define KNOBIFY_ICON_REPEAT "\xEE\x81\x80"      // U+E040
#define KNOBIFY_ICON_REPEAT_ONE "\xEE\x81\x81"  // U+E041
// The options panel's cover/spectrum switch (ADR 0014): the icon shows
// what a tap switches to.
#define KNOBIFY_ICON_IMAGE "\xEE\x8F\xB4"       // U+E3F4
#define KNOBIFY_ICON_EQUALIZER "\xEE\x80\x9D"   // U+E01D

// 48px glyphs for the main menu tiles and the Brightness and Sleep
// screens (ADR 0010, ADR 0015) -- IconFont48.c, same source font.
LV_FONT_DECLARE(knobify_icon_font_48);

#define KNOBIFY_ICON_MUSIC_NOTE "\xEE\x90\x85"  // U+E405
#define KNOBIFY_ICON_SETTINGS "\xEE\xA2\xB8"    // U+E8B8
#define KNOBIFY_ICON_LIGHT_MODE "\xEE\x94\x98"  // U+E518
#define KNOBIFY_ICON_BEDTIME "\xEE\xBD\x84"     // U+EF44
// Audiobooks ("menu_book") and Radio Plays ("theater_comedy") -- the two
// collections added in ADR 0018. A book and a pair of masks read as
// "read to me" and "a play" without a label, which is what a carousel
// tile seen out of the corner of the eye has to do.
#define KNOBIFY_ICON_MENU_BOOK "\xEE\xA8\x99"      // U+EA19
#define KNOBIFY_ICON_THEATER_COMEDY "\xEE\xA9\xA6"  // U+EA66
// Games ("sports_esports"), ADR 0022. A game controller is the one thing
// on this menu that is not played through the speaker, and it has to say
// so from the corner of the eye like the rest of the tiles do.
#define KNOBIFY_ICON_GAMES "\xEE\xA8\xA8"           // U+EA28

// 16px fast_rewind/fast_forward for Now Playing's time pill shuttle marks
// (ADR 0013) -- IconFont16.c, same source font. Only carries these two
// glyphs; its `.fallback` is knobify_text_font_14 so the pill's digits
// and "x" render normally in the same label/font.
LV_FONT_DECLARE(knobify_icon_font_16);

#define KNOBIFY_ICON_FAST_REWIND "\xEE\x80\xA0"  // U+E020
#define KNOBIFY_ICON_FAST_FORWARD "\xEE\x80\x9F"  // U+E01F

#ifdef __cplusplus
}
#endif
