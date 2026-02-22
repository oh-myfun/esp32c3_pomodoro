#include "ui_screen_main.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_MAIN";

static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *humidity_label = NULL;
static lv_obj_t *wifi_status_label = NULL;

lv_obj_t* ui_screen_main_create(void)
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

    wifi_status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(wifi_status_label, "");
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint, "Rotate: nav | SET: adjust");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    ESP_LOGI(TAG, "Main screen created");
    return screen;
}

void ui_screen_main_update_time(void)
{
    if (time_label == NULL || date_label == NULL) return;

    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &timeinfo);
    lv_label_set_text(time_label, time_buf);

    char date_buf[32];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %a", &timeinfo);
    lv_label_set_text(date_label, date_buf);
}

void ui_screen_main_update_temp(float temp)
{
    if (temp_label == NULL) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fC", temp);
    lv_label_set_text(temp_label, buf);
}

void ui_screen_main_update_humidity(float humidity)
{
    if (humidity_label == NULL) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f%%", humidity);
    lv_label_set_text(humidity_label, buf);
}

void ui_screen_main_update_wifi_status(const char *status, uint32_t color)
{
    if (wifi_status_label == NULL) return;
    lv_label_set_text(wifi_status_label, status ? status : "");
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(color), 0);
}
