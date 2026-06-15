#include "ui_helpers.h"
#include "ui_theme.h"
#include "custom_font.h"

lv_obj_t *ui_create_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, UI_COLOR_BG, 0);
    lv_obj_set_size(s, 240, 240);
    return s;
}

lv_obj_t *ui_create_title_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &custom_font_16, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, UI_TITLE_Y_OFFSET);
    return lbl;
}

lv_obj_t *ui_create_hint_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_HINT, 0);
    lv_obj_set_style_text_font(lbl, &custom_font_14, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, UI_HINT_BOTTOM_OFFSET);
    return lbl;
}

void ui_register_callbacks(ui_screen_id_t screen,
                           void (*on_cw)(void),
                           void (*on_ccw)(void),
                           void (*on_press)(void),
                           void (*on_long_press)(void),
                           void (*on_settings_press)(void),
                           void (*on_settings_long_press)(void),
                           const char *(*on_long_press_hint)(bool))
{
    ui_input_callbacks_t cbs = {
        .on_encoder_cw = on_cw,
        .on_encoder_ccw = on_ccw,
        .on_encoder_press = on_press,
        .on_encoder_long_press = on_long_press,
        .on_settings_press = on_settings_press,
        .on_settings_long_press = on_settings_long_press,
        .on_long_press_hint = on_long_press_hint,
    };
    ui_register_input_callbacks(screen, &cbs);
}
