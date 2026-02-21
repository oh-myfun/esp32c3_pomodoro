#include "ui_screen_wifi.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_WIFI";

static lv_obj_t *wifi_list_title = NULL;
static lv_obj_t *wifi_list_labels[8];
static lv_obj_t *wifi_list_hint = NULL;

static lv_obj_t *pwd_title = NULL;
static lv_obj_t *pwd_ssid_label = NULL;
static lv_obj_t *pwd_display = NULL;
static lv_obj_t *pwd_hint = NULL;

lv_obj_t* ui_screen_wifi_list_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    wifi_list_title = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_list_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(wifi_list_title, "WiFi Networks");
    lv_obj_set_style_text_font(wifi_list_title, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_list_title, LV_ALIGN_TOP_MID, 0, 10);

    for (int i = 0; i < 8; i++) {
        wifi_list_labels[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(wifi_list_labels[i], &lv_font_montserrat_14, 0);
        lv_label_set_text(wifi_list_labels[i], "");
        lv_obj_align(wifi_list_labels[i], LV_ALIGN_TOP_LEFT, 10, 32 + i * 22);
    }

    wifi_list_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_list_hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(wifi_list_hint, "Scanning...");
    lv_obj_set_style_text_font(wifi_list_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_list_hint, LV_ALIGN_BOTTOM_MID, 0, -5);

    ESP_LOGI(TAG, "WiFi list screen created");
    return screen;
}

void ui_screen_wifi_list_update(int count, wifi_scan_result_t *results, int selected, const char *hint)
{
    for (int i = 0; i < 8; i++) {
        if (wifi_list_labels[i] == NULL) continue;
        
        if (i < count && results != NULL) {
            char buf[64];
            const char *rssi_icon = "";
            if (results[i].rssi > -50) rssi_icon = "***";
            else if (results[i].rssi > -70) rssi_icon = "**";
            else rssi_icon = "*";
            
            if (results[i].open) {
                snprintf(buf, sizeof(buf), "%s %s[Open]", rssi_icon, results[i].ssid);
            } else {
                snprintf(buf, sizeof(buf), "%s %s", rssi_icon, results[i].ssid);
            }
            lv_label_set_text(wifi_list_labels[i], buf);
            
            if (i == selected) {
                lv_obj_set_style_text_color(wifi_list_labels[i], lv_color_hex(0x00FF00), 0);
            } else {
                lv_obj_set_style_text_color(wifi_list_labels[i], lv_color_hex(0xFFFFFF), 0);
            }
        } else {
            lv_label_set_text(wifi_list_labels[i], "");
        }
    }
    
    if (wifi_list_hint && hint) {
        lv_label_set_text(wifi_list_hint, hint);
    }
}

lv_obj_t* ui_screen_password_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    pwd_title = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(pwd_title, "Password");
    lv_obj_set_style_text_font(pwd_title, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_title, LV_ALIGN_TOP_MID, 0, 5);

    pwd_ssid_label = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_ssid_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(pwd_ssid_label, "SSID: ");
    lv_obj_set_style_text_font(pwd_ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_ssid_label, LV_ALIGN_TOP_LEFT, 5, 25);

    pwd_display = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_display, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(pwd_display, "");
    lv_obj_set_style_text_font(pwd_display, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_display, LV_ALIGN_TOP_LEFT, 5, 50);

    pwd_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(pwd_hint, "Rotate: select | SET: input");
    lv_obj_set_style_text_font(pwd_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_hint, LV_ALIGN_BOTTOM_MID, 0, -5);

    ESP_LOGI(TAG, "Password screen created");
    return screen;
}

void ui_screen_password_set_ssid(const char *ssid)
{
    if (pwd_ssid_label == NULL) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "SSID: %s", ssid ? ssid : "");
    lv_label_set_text(pwd_ssid_label, buf);
}

void ui_screen_password_update_display(const char *password, int cursor_pos)
{
    if (pwd_display == NULL) return;
    lv_label_set_text(pwd_display, password ? password : "");
}

void ui_screen_password_set_hint(const char *hint)
{
    if (pwd_hint == NULL) return;
    lv_label_set_text(pwd_hint, hint ? hint : "");
}
