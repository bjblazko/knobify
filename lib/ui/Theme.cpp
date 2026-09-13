#include "Theme.h"

namespace knobify::ui::theme {

namespace {

lv_theme_t childTheme;
lv_style_t screenStyle;
lv_style_t buttonStyle;
lv_style_t listStyle;
lv_style_t rowStyle;
lv_style_t rowPressedStyle;
lv_style_t rowCheckedStyle;

void initStyles() {
  lv_style_init(&screenStyle);
  lv_style_set_bg_color(&screenStyle, surface());
  lv_style_set_text_color(&screenStyle, ink());

  // Flat -- no shadows or gradients (ux-guidelines §3 rule 4).
  lv_style_init(&buttonStyle);
  lv_style_set_shadow_width(&buttonStyle, 0);

  lv_style_init(&listStyle);
  lv_style_set_bg_opa(&listStyle, LV_OPA_TRANSP);
  lv_style_set_border_width(&listStyle, 0);
  lv_style_set_radius(&listStyle, 0);
  lv_style_set_pad_row(&listStyle, 2);

  // No dividers: spacing separates rows, not lines (as little design as
  // possible).
  lv_style_init(&rowStyle);
  lv_style_set_border_width(&rowStyle, 0);
  lv_style_set_radius(&rowStyle, kRowRadius);
  lv_style_set_bg_opa(&rowStyle, LV_OPA_TRANSP);
  lv_style_set_text_color(&rowStyle, ink());

  lv_style_init(&rowPressedStyle);
  lv_style_set_bg_color(&rowPressedStyle, surfaceAlt());
  lv_style_set_bg_opa(&rowPressedStyle, LV_OPA_COVER);

  // The knob-selected row (ScreenManager::applyHighlight() toggles
  // LV_STATE_CHECKED) -- structure, not accent: selection isn't the
  // screen's primary action.
  lv_style_init(&rowCheckedStyle);
  lv_style_set_bg_color(&rowCheckedStyle, ink());
  lv_style_set_bg_opa(&rowCheckedStyle, LV_OPA_COVER);
  lv_style_set_text_color(&rowCheckedStyle, surface());
}

// Runs after the default theme's own apply_cb (lv_theme_apply() walks
// parents first), so these styles take precedence over its defaults.
void applyCb(lv_theme_t *, lv_obj_t *obj) {
  if (lv_obj_get_parent(obj) == nullptr) {
    // Only bg_color, not bg_opa: lv_layer_top()/lv_layer_sys() are also
    // parentless objects but must stay transparent (LVGL sets that as a
    // local style, which this doesn't touch).
    lv_obj_add_style(obj, &screenStyle, 0);
    return;
  }
  if (lv_obj_check_type(obj, &lv_list_btn_class)) {
    lv_obj_add_style(obj, &rowStyle, 0);
    lv_obj_add_style(obj, &rowPressedStyle, LV_STATE_PRESSED);
    lv_obj_add_style(obj, &rowCheckedStyle, LV_STATE_CHECKED);
    return;
  }
  if (lv_obj_check_type(obj, &lv_btn_class)) {
    lv_obj_add_style(obj, &buttonStyle, 0);
    return;
  }
  if (lv_obj_check_type(obj, &lv_list_class)) {
    lv_obj_add_style(obj, &listStyle, 0);
  }
}

}  // namespace

void apply(lv_disp_t *disp) {
  // Light theme: this is a reflective IPS LCD, not an OLED/AMOLED -- dark
  // UIs (tried first) don't render as well on it (ux-guidelines §3).
  lv_theme_t *base = lv_theme_default_init(disp, ink(), structure(),
                                           /*dark_mode=*/false,
                                           LV_FONT_DEFAULT);
  initStyles();
  childTheme = *base;
  lv_theme_set_parent(&childTheme, base);
  lv_theme_set_apply_cb(&childTheme, &applyCb);
  lv_disp_set_theme(disp, &childTheme);
}

}  // namespace knobify::ui::theme
