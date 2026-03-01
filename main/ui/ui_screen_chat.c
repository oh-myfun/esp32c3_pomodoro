#include "ui_screen_chat.h"
#include "ui_manager.h"
#include "esp_log.h"

static const char *TAG = "UI_CHAT";

static void chat_on_encoder_cw(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

static void chat_on_encoder_ccw(void)
{
    ui_switch_screen(UI_SCREEN_POMODORO);
}

lv_obj_t* ui_screen_chat_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "Chat Assistant");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(hint, "Coming soon...");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = chat_on_encoder_cw,
        .on_encoder_ccw = chat_on_encoder_ccw,
    };
    ui_register_input_callbacks(UI_SCREEN_CHAT, &cbs);

    ESP_LOGI(TAG, "Chat screen created");
    return screen;
}
