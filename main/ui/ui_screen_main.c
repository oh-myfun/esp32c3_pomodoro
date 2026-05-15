#include "ui_screen_main.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "service/time_service.h"
#include "service/wifi_service.h"
#include "driver/st7789_lcd.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_MAIN";

static void main_on_encoder_cw(void)
{
    ui_switch_screen(UI_SCREEN_POMODORO);
}

static void main_on_encoder_ccw(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

static void main_on_settings_press(void)
{
    time_service_request_sync();
    ESP_LOGI(TAG, "Manual time sync triggered");
}

static void main_on_encoder_press(void)
{
    ESP_LOGI(TAG, "Triggering LCD reset");
    st7789_lcd_reset();
}

static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *humidity_label = NULL;
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *hint_label = NULL;

lv_obj_t* ui_screen_main_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_size(screen, 240, 240);

    temp_label = lv_label_create(screen);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFF6B6B), 0);
    lv_label_set_text(temp_label, "--C");
    lv_obj_set_style_text_font(temp_label, &custom_font_16, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 10, 10);

    humidity_label = lv_label_create(screen);
    lv_obj_set_style_text_color(humidity_label, lv_color_hex(0x4D96FF), 0);
    lv_label_set_text(humidity_label, "--%");
    lv_obj_set_style_text_font(humidity_label, &custom_font_16, 0);
    lv_obj_align(humidity_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    time_label = lv_label_create(screen);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(time_label, "12:00:00");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_letter_space(time_label, 2, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -15);

    date_label = lv_label_create(screen);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(date_label, "2025-01-01 Mon");
    lv_obj_set_style_text_font(date_label, &custom_font_16, 0);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 60);

    wifi_status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(wifi_status_label, "");
    lv_obj_set_style_text_font(wifi_status_label, &custom_font_16, 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 28);

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_SET_SYNC));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = main_on_encoder_cw,
        .on_encoder_ccw = main_on_encoder_ccw,
        .on_encoder_press = main_on_encoder_press,
        .on_settings_press = main_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_MAIN, &cbs);

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
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d %s",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             i18n_weekday(timeinfo.tm_wday));
    lv_label_set_text(date_label, date_buf);

    if (hint_label) {
        lv_label_set_text(hint_label, i18n(STR_SET_SYNC));
    }
}

void ui_screen_main_update_temp(float temp)
{
    ESP_LOGD(TAG, "update_temp called (%.1f) but no sensor available, keeping placeholder", temp);
}

void ui_screen_main_update_humidity(float humidity)
{
    ESP_LOGD(TAG, "update_humidity called (%.0f) but no sensor available, keeping placeholder", humidity);
}

void ui_screen_main_update_wifi_status(const char *status, uint32_t color)
{
    if (wifi_status_label == NULL) return;
    lv_label_set_text(wifi_status_label, status ? status : "");
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(color), 0);
}
