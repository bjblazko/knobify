#pragma once

#include <lvgl.h>

namespace knobify::ui::theme {

// knobify's Braun-inspired palette -- the single source of truth for
// every color drawn on screen. Token names and roles are documented in
// docs/design/ux-guidelines.md §3; UI code uses these, never a raw hex
// value or an lv_palette_* color.
// The panel is RGB565 (5/6/5 bits) and shifts toward green. A series of
// "warmed" surfaces (#EFEFE7 .. #E6D3BD) meant to compensate all still
// read as wrongly tinted on the device (green-grey or peach), so the
// neutrals are back to the original Snow White / Light Grey (user
// decision 2026-09-13; ux-guidelines §3 "RGB565").
inline lv_color_t surface() { return lv_color_hex(0xF4F4F0); }     // Snow White
inline lv_color_t surfaceAlt() { return lv_color_hex(0xDCDDD8); }  // Light Grey
inline lv_color_t structure() { return lv_color_hex(0x4A4C4E); }   // Mid Anthracite
inline lv_color_t ink() { return lv_color_hex(0x1E1F21); }         // Matte Black
inline lv_color_t accent() { return lv_color_hex(0xE85D04); }      // Orange Signal
inline lv_color_t confirm() { return lv_color_hex(0x2A8C4A); }     // Functional Green
// Time passing -- the song-progress ring only, after the yellow second
// hand of Braun's clocks. Next to the dark housing at the bezel edge it
// reads clearly, where anthracite blended into the case.
inline lv_color_t time() { return lv_color_hex(0xF5AA1C); }        // Braun Yellow
inline lv_color_t warning() { return lv_color_hex(0xD62828); }     // Accent Red

// Radius for list rows (ux-guidelines §3a); pills and round controls use
// LV_RADIUS_CIRCLE instead.
constexpr lv_coord_t kRowRadius = 12;

// Installs LVGL's default light theme recolored to the palette above,
// plus a small child theme for the app-wide rules the default theme
// doesn't cover (screen background, flat unshadowed buttons, borderless
// lists with rounded, ink-filled selected rows). Call once, after the
// display is registered.
void apply(lv_disp_t *disp);

// The three button roles (ux-guidelines §3a). Applied as
// local styles, so they win over whatever the theme gave the button.
//
// Primary: filled accent circle, white glyph. Exactly one per screen.
inline void stylePrimaryButton(lv_obj_t *btn) {
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn, accent(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(btn, surface(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
}

// Secondary: filled light-grey circle, ink glyph, darker grey while
// pressed. Controls that belong to the primary one (previous/next next to
// Play/Pause) -- like the grey keys beside the one colored key on a Braun
// tape deck.
inline void styleSecondaryButton(lv_obj_t *btn) {
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn, surfaceAlt(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(btn, ink(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
}

// Quiet: no fill, anthracite glyph, light-grey background only while
// pressed. Navigation and utility: back, lock, scan.
inline void styleQuietButton(lv_obj_t *btn) {
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(btn, surfaceAlt(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn, structure(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
}

}  // namespace knobify::ui::theme
