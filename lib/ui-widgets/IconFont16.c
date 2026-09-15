/*******************************************************************************
 * Size: 16 px
 * Bpp: 4
 * Opts: --font MaterialSymbolsOutlined.ttf --range 0xE020,0xE01F --size 16 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h --lv-font-name knobify_icon_font_16 -o IconFont16.c
 *
 * Generated 2026-09-15 via lv_font_conv (npm) from the same Google
 * Material Symbols Outlined variable font as IconFont.c/IconFont48.c
 * (google/material-design-icons, variablefont/MaterialSymbolsOutlined
 * [FILL,GRAD,opsz,wght].ttf, master branch as of this date), licensed
 * Apache License 2.0 (see that repo's LICENSE). Glyphs: U+E020
 * "fast_rewind", U+E01F "fast_forward" -- the Now Playing time pill's
 * shuttle marks (ADR 0013): LV_SYMBOL_* has only single-triangle
 * prev/next, easily confused with the transport buttons right next to
 * this pill, so a real double-triangle fast-wind glyph is used instead.
 *
 * `.fallback = &lv_font_montserrat_14` below lets the pill mix these
 * glyphs with plain digits/"x" in one label -- this font only carries
 * the two icon codepoints, everything else (LVGL 8.3 supports
 * lv_font_t.fallback) falls through to the built-in font already used
 * for the rest of the pill's text.
 *
 * --no-compress is required (LV_USE_FONT_COMPRESSED is 0 in lv_conf.h),
 * see IconFont.c's header. Keep it when regenerating/extending.
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef KNOBIFY_ICON_FONT_16
#define KNOBIFY_ICON_FONT_16 1
#endif

#if KNOBIFY_ICON_FONT_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+E01F "" */
    0x46, 0x0, 0x0, 0x9, 0x10, 0x0, 0x0, 0x5f,
    0xb1, 0x0, 0xe, 0xf6, 0x0, 0x0, 0x5f, 0xde,
    0x50, 0xe, 0xdf, 0xb1, 0x0, 0x5f, 0x8, 0xfa,
    0x1e, 0x73, 0xdf, 0x60, 0x5f, 0x8, 0xfa, 0x1e,
    0x73, 0xdf, 0x60, 0x5f, 0xde, 0x50, 0xe, 0xdf,
    0xb1, 0x0, 0x5f, 0xb1, 0x0, 0xe, 0xf6, 0x0,
    0x0, 0x46, 0x0, 0x0, 0xa, 0x10, 0x0, 0x0,

    /* U+E020 "" */
    0x0, 0x0, 0x5, 0x60, 0x0, 0x0, 0x91, 0x0,
    0x1, 0xbf, 0x70, 0x0, 0x4e, 0xf1, 0x0, 0x5e,
    0xdf, 0x70, 0x9, 0xfc, 0xf1, 0x1b, 0xf9, 0xe,
    0x74, 0xde, 0x53, 0xf1, 0x1b, 0xf9, 0xe, 0x74,
    0xde, 0x53, 0xf1, 0x0, 0x5e, 0xdf, 0x70, 0x9,
    0xfc, 0xf1, 0x0, 0x1, 0xbf, 0x70, 0x0, 0x4e,
    0xf1, 0x0, 0x0, 0x5, 0x60, 0x0, 0x0, 0x91
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 256, .box_w = 14, .box_h = 8, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 56, .adv_w = 256, .box_w = 14, .box_h = 8, .ofs_x = 1, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 57375, .range_length = 2, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t knobify_icon_font_16 = {
#else
lv_font_t knobify_icon_font_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 8,          /*The maximum line height required by the font*/
    .base_line = -4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_14,
#endif
    .user_data = NULL,
};



#endif /*#if KNOBIFY_ICON_FONT_16*/

