#include "ui_helpers.h"
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
        screen = ui_create_screen();
    }

    /* Title: 16px */
    
    lv_obj_t *title = ui_create_title_label(screen, i18n(STR_PRESSURE_INFO));


    /* Table header: y=32 */
    lv_obj_t *hdr = lv_label_create(screen);
    lv_obj_set_style_text_color(hdr, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hdr, &custom_font_16, 0);
    lv_label_set_text(hdr, i18n(STR_PI_HDR));
    lv_obj_set_pos(hdr, 8, 32);

    /* Separator: y=54 */
    lv_obj_t *sep = lv_label_create(screen);
    lv_obj_set_style_text_color(sep, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(sep, &custom_font_16, 0);
    lv_label_set_text(sep, "-----------------------");
    lv_obj_set_pos(sep, 8, 54);

    /* Table rows: y=76,98,120,142 */
    static const str_id_t row_ids[] = {
        STR_PI_DAILY, STR_PI_RAIN, STR_PI_STORM, STR_PI_TYPHOON
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = lv_label_create(screen);
        lv_obj_set_style_text_color(row, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(row, &custom_font_16, 0);
        lv_label_set_text(row, i18n(row_ids[i]));
        lv_obj_set_pos(row, 8, 76 + i * 22);
    }

    /* Note: y=164 */
    lv_obj_t *note = lv_label_create(screen);
    lv_obj_set_style_text_color(note, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(note, &custom_font_16, 0);
    lv_label_set_text(note, i18n(STR_PI_NOTE));
    lv_obj_set_pos(note, 8, 164);

    /* Hint: 14px */
    lv_obj_t *
    hint = ui_create_hint_label(screen, i18n(STR_PRESSURE_TIP));


    static const ui_input_callbacks_t cbs = {
        .on_encoder_press = go_back,
        .on_encoder_long_press = NULL,
        .on_settings_press = go_back,
    };
    ui_register_input_callbacks(UI_SCREEN_PRESSURE_INFO, &cbs);

    ESP_LOGI(TAG, "Pressure info screen created");
    return screen;
}
