#include "ui_screen_wifi.h"
#include "i18n.h"
#include "custom_font.h"
#include "service/wifi_service.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "ui_text_input.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_WIFI";

static char selected_ssid[33] = {0};

static void on_wifi_password_result(const char *result);

static void wifi_on_encoder_cw(void)
{
    lv_obj_t *list = ui_screen_wifi_list_get_list();
    if (list) ui_list_nav_next(list);
}

static void wifi_on_encoder_ccw(void)
{
    lv_obj_t *list = ui_screen_wifi_list_get_list();
    if (list) ui_list_nav_prev(list);
}

static void wifi_on_encoder_press(void)
{
    ui_go_back();
}

static void wifi_on_settings_press(void)
{
    int count = wifi_service_get_scan_count();
    int selected = ui_screen_wifi_list_get_selected();
    if (count > 0) {
        const wifi_ap_info_t *result = wifi_service_get_ap(selected);
        if (result) {
            strncpy(selected_ssid, (const char *)result->ssid, sizeof(selected_ssid) - 1);
            char pwd_title[48];
            snprintf(pwd_title, sizeof(pwd_title), "%s %s", i18n(STR_T_PASSWORD), selected_ssid);
            ui_text_input_configure(pwd_title, "", TEXT_INPUT_TEXT, 63, on_wifi_password_result);
            ui_switch_screen(UI_SCREEN_TEXT_INPUT);
        }
    }
}

static lv_obj_t *wifi_list_title = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *wifi_list_hint = NULL;

// 静态数组用于存储WiFi列表项的键值，避免在ui_screen_wifi_list_update中频繁分配/释放内存
static char wifi_item_keys[20][33];
static char wifi_item_values[20][48];

static const wifi_ap_info_t *wifi_results = NULL;
static int wifi_count = 0;

static void on_wifi_password_result(const char *result)
{
    if (result) {
        wifi_service_connect(selected_ssid, result);
    }
}

static void wifi_list_item_click(int index)
{
    if (index < 0 || index >= wifi_count || !wifi_results) return;

    strncpy(selected_ssid, (char*)wifi_results[index].ssid, sizeof(selected_ssid) - 1);
    if (wifi_results[index].open) {
        wifi_service_connect(wifi_results[index].ssid, "");
        ui_go_back();
    } else {
        char pwd_title[48];
        snprintf(pwd_title, sizeof(pwd_title), "%s %s", i18n(STR_T_PASSWORD), selected_ssid);
        ui_text_input_configure(pwd_title, "", TEXT_INPUT_TEXT, 63, on_wifi_password_result);
        ui_switch_screen(UI_SCREEN_TEXT_INPUT);
    }
}

lv_obj_t* ui_screen_wifi_list_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);
    lv_obj_set_pos(screen, 0, 0);

    wifi_list_title = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_list_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(wifi_list_title, i18n(STR_WIFI_NETWORKS));
    lv_obj_set_style_text_font(wifi_list_title, &custom_font_16, 0);
    lv_obj_align(wifi_list_title, LV_ALIGN_TOP_MID, 0, 10);

    // 创建列表，高度设为刚好显示8个项目 (8 * 22 = 176)
    wifi_list = ui_list_create(screen, 220, 196, 10, 30);
    ui_list_set_click_callback(wifi_list, wifi_list_item_click);

    wifi_list_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_list_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(wifi_list_hint, i18n(STR_H_SET_SELECT_PRESS_BACK));
    lv_obj_set_style_text_font(wifi_list_hint, &custom_font_14, 0);
    lv_obj_align(wifi_list_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = wifi_on_encoder_cw,
        .on_encoder_ccw = wifi_on_encoder_ccw,
        .on_encoder_press = wifi_on_encoder_press,
        .on_settings_press = wifi_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_WIFI_LIST, &cbs);

    ESP_LOGI(TAG, "WiFi list screen created");
    return screen;
}

lv_obj_t* ui_screen_wifi_list_get_list(void)
{
    return wifi_list;
}

void ui_screen_wifi_list_update(int count, wifi_ap_info_t *results, int selected, const char *hint)
{
    wifi_results = results;
    wifi_count = count;

    if (wifi_list && count > 0 && results) {
        static ui_list_item_t items[20];
        int item_count = count > 20 ? 20 : count;

        for (int i = 0; i < item_count; i++) {
            wifi_ap_info_t *ap = &results[i];

            // SSID作为key，如果是开放网络则加上[open]
            bool is_saved = wifi_service_is_saved((const char*)ap->ssid);
            if (ap->open) {
                snprintf(wifi_item_keys[i], sizeof(wifi_item_keys[i]), "%s%.*s%s",
                         is_saved ? "●" : "", is_saved ? 28 : 32, ap->ssid, i18n(STR_OPEN_NET));
            } else {
                snprintf(wifi_item_keys[i], sizeof(wifi_item_keys[i]), "%s%.*s",
                         is_saved ? "●" : "", is_saved ? 29 : 32, ap->ssid);
            }

            // WiFi signal strength: block bars (min 1, max 5)
            static const char * const signal_bars[] = {
                "█",         //  1 (weakest)
                "██",        //  2
                "███",       //  3
                "████",      //  4
                "█████",     //  5 (strongest)
            };
            int signal_level;
            if      (ap->rssi > -45) signal_level = 4;
            else if (ap->rssi > -55) signal_level = 3;
            else if (ap->rssi > -65) signal_level = 2;
            else if (ap->rssi > -75) signal_level = 1;
            else                     signal_level = 0;

            snprintf(wifi_item_values[i], sizeof(wifi_item_values[i]), "%s",
                     signal_level > 0 ? signal_bars[signal_level - 1] : "");

            items[i].key = wifi_item_keys[i];
            items[i].value = wifi_item_values[i];
        }

        // 保存当前选中的位置
        int current_selection = ui_list_get_selected(wifi_list);

        ui_list_set_items(wifi_list, items, item_count);

        // 恢复之前选中的位置，如果它仍然有效
        if (current_selection < item_count) {
            ui_list_set_selected(wifi_list, current_selection);
        } else if (selected >= 0 && selected < item_count) {
            // 否则，使用传入的selected参数
            ui_list_set_selected(wifi_list, selected);
        } else {
            // 如果都没有有效值，则默认选中第一个
            ui_list_set_selected(wifi_list, 0);
        }
    }

    if (wifi_list_hint && hint) {
        lv_label_set_text(wifi_list_hint, hint);
    }
}

static int last_scan_count = -1;

void ui_screen_wifi_list_refresh(void)
{
    if (wifi_list_title) {
        lv_label_set_text(wifi_list_title, i18n(STR_WIFI_NETWORKS));
    }
    if (wifi_list_hint) {
        lv_label_set_text(wifi_list_hint, i18n(STR_H_SET_SELECT_PRESS_BACK));
    }

    int count = wifi_service_get_scan_count();

    // Only update when scan count changes
    if (count == last_scan_count) return;
    last_scan_count = count;

    const wifi_ap_info_t *results = NULL;
    if (count > 0) {
        results = wifi_service_get_ap(0);
    }

    const char *hint = count > 0 ? i18n(STR_H_SET_SELECT_PRESS_BACK) : i18n(STR_SCANNING);
    ui_screen_wifi_list_update(count, (wifi_ap_info_t*)results, 0, hint);
}

int ui_screen_wifi_list_get_selected(void)
{
    if (wifi_list) {
        return ui_list_get_selected(wifi_list);
    }
    return 0;
}

const char* ui_screen_wifi_list_get_selected_ssid(void)
{
    return selected_ssid;
}
