#include "ui_screen_settings.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS";

static lv_obj_t *settings_title = NULL;
static lv_obj_t *settings_item_labels[6];
static lv_obj_t *settings_value_labels[6];
static lv_obj_t *settings_hint = NULL;

static const char *settings_names[6] = {
    "Brightness",
    "Contrast", 
    "Language",
    "Pomodoro",
    "WiFi"
};

lv_obj_t* ui_screen_settings_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    settings_title = lv_label_create(screen);
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(settings_title, "SETTINGS");
    lv_obj_set_style_text_font(settings_title, &lv_font_montserrat_14, 0);
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 15);

    for (int i = 0; i < 5; i++) {
        int y_offset = 45 + i * 40;

        settings_item_labels[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(settings_item_labels[i], &lv_font_montserrat_14, 0);
        lv_label_set_text(settings_item_labels[i], settings_names[i]);
        lv_obj_align(settings_item_labels[i], LV_ALIGN_TOP_LEFT, 20, y_offset);

        settings_value_labels[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(settings_value_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_align(settings_value_labels[i], LV_ALIGN_TOP_RIGHT, -20, y_offset);
    }

    settings_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(settings_hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(settings_hint, "Press SET to adjust");
    lv_obj_set_style_text_font(settings_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(settings_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    ESP_LOGI(TAG, "Settings screen created");
    return screen;
}

void ui_screen_settings_update(int *values, int selected, settings_mode_t mode)
{
    static const char *language_options[] = {"English", "Chinese"};
    static const int language_count = 2;

    for (int i = 0; i < 5; i++) {
        if (settings_item_labels[i] == NULL || settings_value_labels[i] == NULL) continue;

        if (i == selected && mode == SETTINGS_MODE_SELECT) {
            lv_obj_set_style_text_color(settings_item_labels[i], lv_color_hex(0x00FF00), 0);
            lv_obj_set_style_text_color(settings_value_labels[i], lv_color_hex(0x00FF00), 0);
        } else if (i == selected && mode == SETTINGS_MODE_ADJUST) {
            lv_obj_set_style_text_color(settings_item_labels[i], lv_color_hex(0xFFFF00), 0);
            lv_obj_set_style_text_color(settings_value_labels[i], lv_color_hex(0xFFFF00), 0);
        } else {
            lv_obj_set_style_text_color(settings_item_labels[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_color(settings_value_labels[i], lv_color_hex(0xAAAAAA), 0);
        }

        char buf[20];
        switch (i) {
            case 2:  // Language
                snprintf(buf, sizeof(buf), "%s", language_options[values[i] % language_count]);
                break;
            case 4:  // WiFi
                snprintf(buf, sizeof(buf), ">");
                break;
            default:
                snprintf(buf, sizeof(buf), "%d", values[i]);
                break;
        }
        lv_label_set_text(settings_value_labels[i], buf);
    }
}

void ui_screen_settings_set_hint(const char *hint)
{
    if (settings_hint == NULL) return;
    lv_label_set_text(settings_hint, hint);
}
