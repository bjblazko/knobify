#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// These four fonts REPLACE LVGL's built-in lv_font_montserrat_14/16/20/28
// (switched off via LV_USE_FONT_MONTSERRAT_* 0 in lv_conf.h). They are
// generated from the exact same Montserrat-Medium.ttf LVGL's own
// built-ins came from, so glyph shapes/metrics match, but additionally
// cover Latin-1 Supplement (U+00A0-U+00FF) and Latin Extended-A
// (U+0100-U+017F) -- umlauts, accents and other European letters that
// show up in music tags -- plus en/em dash, curly quotes, bullet and
// ellipsis. They also carry the same FontAwesome LV_SYMBOL_* glyph
// range as the built-ins they replace, so every existing LV_SYMBOL_*
// use keeps working unchanged.
//
// See TextFont14.c/16.c/20.c/28.c's header comments for the exact
// lv_font_conv command, sources and licences, and each font's struct
// comment for why .line_height/.base_line are hand-pinned to the
// built-in Montserrat fonts' own values. Regenerate with
// scripts/generate-text-fonts.sh, never by hand.
LV_FONT_DECLARE(knobify_text_font_14);
LV_FONT_DECLARE(knobify_text_font_16);
LV_FONT_DECLARE(knobify_text_font_20);
LV_FONT_DECLARE(knobify_text_font_28);

#ifdef __cplusplus
}
#endif
