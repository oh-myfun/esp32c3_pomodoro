#include "ui_manager.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

static const char *TAG = "UI";

static lv_obj_t *screens[UI_SCREEN_COUNT];
static ui_screen_id_t current_screen = UI_SCREEN_MAIN;
static settings_mode_t settings_mode = SETTINGS_MODE_IDLE;

// 主界面控件
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *humidity_label = NULL;

// 设置界面控件
static lv_obj_t *settings_title = NULL;
static lv_obj_t *settings_item_labels[SETTINGS_COUNT];
static lv_obj_t *settings_value_labels[SETTINGS_COUNT];
static lv_obj_t *settings_hint = NULL;

// 设置项当前值
static int settings_values[SETTINGS_COUNT] = {50, 50, 0};  // 亮度、对比度、语言
static int current_settings_item = 0;

// 设置项名称
static const char *settings_names[SETTINGS_COUNT] = {
    "Brightness",
    "Contrast",
    "Language"
};

// 语言选项
static const char *language_options[] = {"English", "Chinese"};
static const int language_count = 2;

// 更新设置界面显示
static void update_settings_display(void)
{
    if (current_screen != UI_SCREEN_SETTINGS) return;

    for (int i = 0; i < SETTINGS_COUNT; i++) {
        if (settings_item_labels[i] == NULL || settings_value_labels[i] == NULL) continue;

        // 高亮当前选中项
        if (i == current_settings_item && settings_mode == SETTINGS_MODE_SELECT) {
            lv_obj_set_style_text_color(settings_item_labels[i], lv_color_hex(0x00FF00), 0);
            lv_obj_set_style_text_color(settings_value_labels[i], lv_color_hex(0x00FF00), 0);
        } else if (i == current_settings_item && settings_mode == SETTINGS_MODE_ADJUST) {
            lv_obj_set_style_text_color(settings_item_labels[i], lv_color_hex(0xFFFF00), 0);
            lv_obj_set_style_text_color(settings_value_labels[i], lv_color_hex(0xFFFF00), 0);
        } else {
            lv_obj_set_style_text_color(settings_item_labels[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_color(settings_value_labels[i], lv_color_hex(0xAAAAAA), 0);
        }

        // 更新值显示
        char buf[16];
        if (i == SETTINGS_LANGUAGE) {
            sprintf(buf, "%s", language_options[settings_values[i] % language_count]);
        } else {
            sprintf(buf, "%d", settings_values[i]);
        }
        lv_label_set_text(settings_value_labels[i], buf);
    }

    // 更新提示
    if (settings_mode == SETTINGS_MODE_SELECT) {
        lv_label_set_text(settings_hint, "Press to adjust");
    } else if (settings_mode == SETTINGS_MODE_ADJUST) {
        lv_label_set_text(settings_hint, "Adjusting...");
    }
}

// 主界面
lv_obj_t *ui_create_main_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_size(screen, 240, 240);

    temp_label = lv_label_create(screen);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFF6B6B), 0);
    lv_label_set_text(temp_label, "25C");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_14, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 10, 10);

    humidity_label = lv_label_create(screen);
    lv_obj_set_style_text_color(humidity_label, lv_color_hex(0x4D96FF), 0);
    lv_label_set_text(humidity_label, "65%");
    lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_14, 0);
    lv_obj_align(humidity_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    time_label = lv_label_create(screen);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(time_label, "12:00:00");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_letter_space(time_label, 2, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -15);

    date_label = lv_label_create(screen);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(date_label, "2025-01-01 Mon");
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(hint, "Press SET to config");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);

    return screen;
}

// 设置界面
lv_obj_t *ui_create_settings_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    settings_title = lv_label_create(screen);
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(settings_title, "SETTINGS");
    lv_obj_set_style_text_font(settings_title, &lv_font_montserrat_14, 0);
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 15);

    // 创建设置项
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        int y_offset = 45 + i * 50;

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

    return screen;
}

void ui_init(void)
{
    screens[UI_SCREEN_MAIN] = ui_create_main_screen();
    screens[UI_SCREEN_SETTINGS] = ui_create_settings_screen();

    lv_scr_load(screens[UI_SCREEN_MAIN]);
    current_screen = UI_SCREEN_MAIN;
    settings_mode = SETTINGS_MODE_IDLE;

    ESP_LOGI(TAG, "UI initialized: %d screens", UI_SCREEN_COUNT);
}

void ui_switch_screen(ui_screen_id_t screen_id)
{
    if (screen_id >= UI_SCREEN_COUNT) return;
    if (screen_id == current_screen) return;

    lv_scr_load(screens[screen_id]);
    current_screen = screen_id;
}

ui_screen_id_t ui_get_current_screen(void)
{
    return current_screen;
}

settings_mode_t ui_get_settings_mode(void)
{
    return settings_mode;
}

void ui_enter_settings(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        settings_mode = SETTINGS_MODE_SELECT;
        current_settings_item = 0;
        ui_switch_screen(UI_SCREEN_SETTINGS);
        update_settings_display();
        ESP_LOGI(TAG, "Enter settings mode");
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_ADJUST;
        update_settings_display();
        ESP_LOGI(TAG, "Enter adjust mode: %s", settings_names[current_settings_item]);
    }
}

void ui_exit_settings(void)
{
    if (settings_mode == SETTINGS_MODE_ADJUST) {
        settings_mode = SETTINGS_MODE_SELECT;
        update_settings_display();
        ESP_LOGI(TAG, "Exit adjust mode");
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_IDLE;
        ui_switch_screen(UI_SCREEN_MAIN);
        ESP_LOGI(TAG, "Exit settings mode");
    }
}

void ui_settings_select_next(void)
{
    if (settings_mode != SETTINGS_MODE_SELECT) return;
    current_settings_item = (current_settings_item + 1) % SETTINGS_COUNT;
    update_settings_display();
}

void ui_settings_select_prev(void)
{
    if (settings_mode != SETTINGS_MODE_SELECT) return;
    current_settings_item = (current_settings_item - 1 + SETTINGS_COUNT) % SETTINGS_COUNT;
    update_settings_display();
}

void ui_settings_enter_adjust(void)
{
    ui_enter_settings();
}

void ui_settings_adjust_up(void)
{
    if (settings_mode != SETTINGS_MODE_ADJUST) return;

    switch (current_settings_item) {
        case SETTINGS_BRIGHTNESS:
        case SETTINGS_CONTRAST:
            if (settings_values[current_settings_item] < 100) {
                settings_values[current_settings_item]++;
            }
            break;
        case SETTINGS_LANGUAGE:
            settings_values[current_settings_item] = (settings_values[current_settings_item] + 1) % language_count;
            break;
    }
    update_settings_display();
    ESP_LOGI(TAG, "%s: %d", settings_names[current_settings_item], settings_values[current_settings_item]);
}

void ui_settings_adjust_down(void)
{
    if (settings_mode != SETTINGS_MODE_ADJUST) return;

    switch (current_settings_item) {
        case SETTINGS_BRIGHTNESS:
        case SETTINGS_CONTRAST:
            if (settings_values[current_settings_item] > 0) {
                settings_values[current_settings_item]--;
            }
            break;
        case SETTINGS_LANGUAGE:
            settings_values[current_settings_item] = (settings_values[current_settings_item] - 1 + language_count) % language_count;
            break;
    }
    update_settings_display();
    ESP_LOGI(TAG, "%s: %d", settings_names[current_settings_item], settings_values[current_settings_item]);
}

void ui_update_time(void)
{
    if (time_label == NULL) return;

    struct timeval tv;
    struct tm *timeinfo;

    gettimeofday(&tv, NULL);
    timeinfo = localtime(&tv.tv_sec);

    if (timeinfo == NULL) return;

    char time_buf[16];
    sprintf(time_buf, "%02d:%02d:%02d",
            timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    lv_label_set_text(time_label, time_buf);

    if (date_label != NULL) {
        char date_buf[32];
        sprintf(date_buf, "%04d-%02d-%02d %s",
                timeinfo->tm_year + 1900,
                timeinfo->tm_mon + 1,
                timeinfo->tm_mday,
                timeinfo->tm_wday == 0 ? "Sun" :
                timeinfo->tm_wday == 1 ? "Mon" :
                timeinfo->tm_wday == 2 ? "Tue" :
                timeinfo->tm_wday == 3 ? "Wed" :
                timeinfo->tm_wday == 4 ? "Thu" :
                timeinfo->tm_wday == 5 ? "Fri" : "Sat");
        lv_label_set_text(date_label, date_buf);
    }
}

void ui_update_temp(float temp)
{
    if (temp_label == NULL) return;
    char buf[16];
    sprintf(buf, "%.1fC", temp);
    lv_label_set_text(temp_label, buf);
}

void ui_update_humidity(float humidity)
{
    if (humidity_label == NULL) return;
    char buf[16];
    sprintf(buf, "%.0f%%", humidity);
    lv_label_set_text(humidity_label, buf);
}
