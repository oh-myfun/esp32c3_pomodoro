#include "ui_screen_pressure_info.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "esp_log.h"

static const char *TAG = "UI_PRESSURE_INFO";

static lv_obj_t *screen = NULL;

static void go_back(void)
{
    ui_go_back();
}

lv_obj_t *ui_screen_pressure_info_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_PRESSURE_INFO));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    /* Table header */
    lv_obj_t *hdr = lv_label_create(screen);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(hdr, &custom_font_14, 0);
    lv_label_set_text(hdr, i18n(STR_PI_HDR));
    lv_obj_set_pos(hdr, 10, 32);

    /* Separator */
    lv_obj_t *sep = lv_label_create(screen);
    lv_obj_set_style_text_color(sep, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(sep, &custom_font_14, 0);
    lv_label_set_text(sep, "--------------------");
    lv_obj_set_pos(sep, 10, 48);

    /* Table rows */
    static const str_id_t row_ids[] = {
        STR_PI_DAILY, STR_PI_RAIN, STR_PI_STORM, STR_PI_TYPHOON
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = lv_label_create(screen);
        lv_obj_set_style_text_color(row, lv_color_hex(0xFFCC00), 0);
        lv_obj_set_style_text_font(row, &custom_font_14, 0);
        lv_label_set_text(row, i18n(row_ids[i]));
        lv_obj_set_pos(row, 10, 66 + i * 18);
    }

    /* Note */
    lv_obj_t *note = lv_label_create(screen);
    lv_obj_set_style_text_color(note, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(note, &custom_font_14, 0);
    lv_label_set_text(note, i18n(STR_PI_NOTE));
    lv_obj_set_pos(note, 10, 142);

    /* Hint */
    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint, i18n(STR_PRESSURE_TIP));
    lv_obj_set_style_text_font(hint, &custom_font_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_press = go_back,
        .on_encoder_long_press = go_back,
        .on_settings_press = go_back,
    };
    ui_register_input_callbacks(UI_SCREEN_PRESSURE_INFO, &cbs);

    ESP_LOGI(TAG, "Pressure info screen created");
    return screen;
}
