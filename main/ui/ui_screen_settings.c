#include "ui_screen_settings.h"
#include "ui_manager.h"
#include "network/wifi_manager.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS";

static void settings_on_encoder_cw(void)
{
    settings_mode_t mode = ui_get_settings_mode();
    if (mode == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_MAIN);
    } else if (mode == SETTINGS_MODE_SELECT) {
        ui_settings_select_next();
    } else if (mode == SETTINGS_MODE_ADJUST) {
        ui_settings_adjust_up();
    }
}

static void settings_on_encoder_ccw(void)
{
    settings_mode_t mode = ui_get_settings_mode();
    if (mode == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_CHAT);
    } else if (mode == SETTINGS_MODE_SELECT) {
        ui_settings_select_prev();
    } else if (mode == SETTINGS_MODE_ADJUST) {
        ui_settings_adjust_down();
    }
}

static void settings_on_encoder_press(void)
{
    settings_mode_t mode = ui_get_settings_mode();
    if (mode == SETTINGS_MODE_ADJUST || mode == SETTINGS_MODE_SELECT) {
        ui_exit_settings();
    }
}

static void settings_on_settings_press(void)
{
    settings_mode_t mode = ui_get_settings_mode();
    if (mode == SETTINGS_MODE_IDLE) {
        ui_enter_settings();
    } else if (mode == SETTINGS_MODE_SELECT) {
        int item = ui_screen_settings_get_current_item();
        if (item == 4) {  // WiFi
            wifi_manager_scan_start();
            ui_switch_screen(UI_SCREEN_WIFI_LIST);
        } else if (item == 3) {  // Pomodoro
            ui_switch_screen(UI_SCREEN_SETTINGS_POMODORO);
        } else {
            ui_settings_enter_adjust();
        }
    } else if (mode == SETTINGS_MODE_ADJUST) {
        ui_exit_settings();
    }
}

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
    lv_obj_set_style_text_font(settings_title, &lv_font_montserrat_16, 0);
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 15);

    for (int i = 0; i < 5; i++) {
        int y_offset = 38 + i * 34;

        settings_item_labels[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(settings_item_labels[i], &lv_font_montserrat_16, 0);
        lv_label_set_text(settings_item_labels[i], settings_names[i]);
        lv_obj_align(settings_item_labels[i], LV_ALIGN_TOP_LEFT, 20, y_offset);

        settings_value_labels[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(settings_value_labels[i], &lv_font_montserrat_16, 0);
        lv_obj_align(settings_value_labels[i], LV_ALIGN_TOP_RIGHT, -20, y_offset);
    }

    settings_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(settings_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(settings_hint, "Rotate: nav | SET: select");
    lv_obj_set_style_text_font(settings_hint, &lv_font_montserrat_16, 0);
    lv_obj_align(settings_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = settings_on_encoder_cw,
        .on_encoder_ccw = settings_on_encoder_ccw,
        .on_encoder_press = settings_on_encoder_press,
        .on_settings_press = settings_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS, &cbs);

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

static settings_mode_t settings_mode = SETTINGS_MODE_IDLE;
static int settings_values[5] = {50, 50, 0, 25, 0};
static int current_settings_item = 0;

void ui_screen_settings_enter(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        settings_mode = SETTINGS_MODE_SELECT;
        current_settings_item = 0;
    }
}

void ui_screen_settings_exit(void)
{
    if (settings_mode == SETTINGS_MODE_ADJUST) {
        settings_mode = SETTINGS_MODE_SELECT;
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_IDLE;
    }
}

settings_mode_t ui_screen_settings_get_mode(void)
{
    return settings_mode;
}

void ui_screen_settings_set_mode(settings_mode_t mode)
{
    settings_mode = mode;
}

void ui_screen_settings_select_next(void)
{
    if (settings_mode != SETTINGS_MODE_SELECT) return;
    current_settings_item = (current_settings_item + 1) % 5;
    ui_screen_settings_update(settings_values, current_settings_item, settings_mode);
}

void ui_screen_settings_select_prev(void)
{
    if (settings_mode != SETTINGS_MODE_SELECT) return;
    current_settings_item = (current_settings_item - 1 + 5) % 5;
    ui_screen_settings_update(settings_values, current_settings_item, settings_mode);
}

void ui_screen_settings_enter_adjust(void)
{
    if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_ADJUST;
        ui_screen_settings_update(settings_values, current_settings_item, settings_mode);
    }
}

void ui_screen_settings_adjust_up(void)
{
    if (settings_mode != SETTINGS_MODE_ADJUST) return;
    
    switch (current_settings_item) {
        case 0:  // Brightness
        case 1:  // Contrast
            if (settings_values[current_settings_item] < 100) {
                settings_values[current_settings_item]++;
            }
            break;
        case 2:  // Language
            settings_values[current_settings_item] = (settings_values[current_settings_item] + 1) % 2;
            break;
        case 3:  // Pomodoro work time
            if (settings_values[current_settings_item] < 60) {
                settings_values[current_settings_item]++;
            }
            break;
    }
    ui_screen_settings_update(settings_values, current_settings_item, settings_mode);
}

void ui_screen_settings_adjust_down(void)
{
    if (settings_mode != SETTINGS_MODE_ADJUST) return;
    
    switch (current_settings_item) {
        case 0:  // Brightness
        case 1:  // Contrast
            if (settings_values[current_settings_item] > 0) {
                settings_values[current_settings_item]--;
            }
            break;
        case 2:  // Language
            settings_values[current_settings_item] = (settings_values[current_settings_item] - 1 + 2) % 2;
            break;
        case 3:  // Pomodoro work time
            if (settings_values[current_settings_item] > 1) {
                settings_values[current_settings_item]--;
            }
            break;
    }
    ui_screen_settings_update(settings_values, current_settings_item, settings_mode);
}

int ui_screen_settings_get_current_item(void)
{
    return current_settings_item;
}

int* ui_screen_settings_get_values(void)
{
    return settings_values;
}
