#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Custom 28px icon font holding just the two glyphs this project needs
// so far (lock, lock_open) -- see IconFont.c's header comment for the
// exact source/version/license, and ADR 0005 for why a custom font
// rather than LV_SYMBOL_* (LVGL's built-in symbol subset has no lock
// icon at all).
LV_FONT_DECLARE(knobify_icon_font_28);

// UTF-8 encodings of the two glyphs, for lv_label_set_text() calls --
// matches how LV_SYMBOL_* constants are used elsewhere in this codebase.
#define KNOBIFY_ICON_LOCK_OPEN "\xEE\xA2\x98"  // U+E898
#define KNOBIFY_ICON_LOCK "\xEE\xA2\x99"       // U+E899

// 48px glyphs for the main menu tiles and the Brightness screen (ADR
// 0010) -- IconFont48.c, same source font.
LV_FONT_DECLARE(knobify_icon_font_48);

#define KNOBIFY_ICON_MUSIC_NOTE "\xEE\x90\x85"  // U+E405
#define KNOBIFY_ICON_SETTINGS "\xEE\xA2\xB8"    // U+E8B8
#define KNOBIFY_ICON_LIGHT_MODE "\xEE\x94\x98"  // U+E518

#ifdef __cplusplus
}
#endif
