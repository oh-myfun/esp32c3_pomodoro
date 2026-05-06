#include "ui_screen_wifi.h"
#include "service/wifi_service.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_WIFI";

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
    ui_switch_screen(UI_SCREEN_WIFI_SAVED);
}

static void wifi_on_settings_press(void)
{
    int count = wifi_service_get_scan_count();
    int selected = ui_screen_wifi_list_get_selected();
    if (count > 0) {
        const wifi_ap_info_t *result = wifi_service_get_ap(selected);
        if (result) {
            ui_switch_screen(UI_SCREEN_PASSWORD_INPUT);
            ui_screen_password_start((const char *)result->ssid);
        }
    }
}

static lv_obj_t *wifi_list_title = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *wifi_list_hint = NULL;

static char selected_ssid[33] = {0};

static lv_obj_t *pwd_title = NULL;
static lv_obj_t *pwd_ssid_label = NULL;
static lv_obj_t *pwd_display = NULL;
static lv_obj_t *pwd_hint = NULL;
static lv_obj_t *pwd_keyboard[4][10];

static char password_buffer[64] = {0};
static int password_len = 0;
static bool pwd_uppercase = false;
static int pwd_selected_row = 0;
static int pwd_selected_col = 0;

// 静态数组用于存储WiFi列表项的键值，避免在ui_screen_wifi_list_update中频繁分配/释放内存
static char wifi_item_keys[20][33];
static char wifi_item_values[20][32];

/* Password input screen callbacks */
static void pwd_on_encoder_cw(void)
{
    ui_screen_password_char_next();
}

static void pwd_on_encoder_ccw(void)
{
    ui_screen_password_char_prev();
}

static void pwd_on_encoder_press(void)
{
    ui_switch_screen(UI_SCREEN_WIFI_SAVED);
}

static void pwd_on_settings_press(void)
{
    ui_screen_password_add_char();
}

static void pwd_on_encoder_long_press(void)
{
    if (password_len > 0) {
        ui_screen_password_delete_char();
    } else {
        ui_switch_screen(UI_SCREEN_WIFI_SAVED);
    }
}

static const char pwd_keys[4][10] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm._"
};

static const wifi_ap_info_t *wifi_results = NULL;
static int wifi_count = 0;

static void wifi_list_item_click(int index)
{
    if (index < 0 || index >= wifi_count || !wifi_results) return;
    
    strncpy(selected_ssid, (char*)wifi_results[index].ssid, sizeof(selected_ssid) - 1);
    if (wifi_results[index].open) {
        wifi_service_connect(wifi_results[index].ssid, "");
        ui_switch_screen(UI_SCREEN_WIFI_SAVED);
    } else {
        ui_switch_screen(UI_SCREEN_PASSWORD_INPUT);
        ui_screen_password_start((char*)wifi_results[index].ssid);
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
    lv_label_set_text(wifi_list_title, "WiFi Networks");
    lv_obj_set_style_text_font(wifi_list_title, &lv_font_montserrat_16, 0);
    lv_obj_align(wifi_list_title, LV_ALIGN_TOP_MID, 0, 10);

    // 创建列表，高度设为刚好显示8个项目 (8 * 22 = 176)
    wifi_list = ui_list_create(screen, 220, 176, 10, 32);
    ui_list_set_click_callback(wifi_list, wifi_list_item_click);

    wifi_list_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_list_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(wifi_list_hint, "SET:select|Press:back");
    lv_obj_set_style_text_font(wifi_list_hint, &lv_font_montserrat_14, 0);
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
                snprintf(wifi_item_keys[i], sizeof(wifi_item_keys[i]), "%s%.*s[open]",
                         is_saved ? "*" : "", 32 - 8, ap->ssid);
            } else {
                snprintf(wifi_item_keys[i], sizeof(wifi_item_keys[i]), "%s%.*s",
                         is_saved ? "*" : "", 32 - 1, ap->ssid);
            }
            
            // 信号强度作为value，10档竖线表示
            const char *rssi_bars = "";
            if (ap->rssi > -40) rssi_bars = "||||||||||";
            else if (ap->rssi > -50) rssi_bars = "|||||||||";
            else if (ap->rssi > -60) rssi_bars = "||||||||";
            else if (ap->rssi > -70) rssi_bars = "|||||||";
            else if (ap->rssi > -80) rssi_bars = "||||||";
            else if (ap->rssi > -85) rssi_bars = "|||||";
            else if (ap->rssi > -90) rssi_bars = "||||";
            else if (ap->rssi > -95) rssi_bars = "|||";
            else if (ap->rssi > -100) rssi_bars = "||";
            else rssi_bars = "|";

            snprintf(wifi_item_values[i], sizeof(wifi_item_values[i]), "%s", rssi_bars);
            
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

lv_obj_t* ui_screen_password_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    pwd_title = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(pwd_title, "Password");
    lv_obj_set_style_text_font(pwd_title, &lv_font_montserrat_16, 0);
    lv_obj_align(pwd_title, LV_ALIGN_TOP_MID, 0, 5);

    pwd_ssid_label = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_ssid_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(pwd_ssid_label, "SSID: ");
    lv_obj_set_style_text_font(pwd_ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_align(pwd_ssid_label, LV_ALIGN_TOP_MID, 0, 28);

    pwd_display = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_display, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(pwd_display, "");
    lv_obj_set_style_text_font(pwd_display, &lv_font_montserrat_16, 0);
    lv_obj_align(pwd_display, LV_ALIGN_TOP_MID, 0, 50);

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 10; col++) {
            pwd_keyboard[row][col] = lv_label_create(screen);
            lv_obj_set_style_text_font(pwd_keyboard[row][col], &lv_font_montserrat_16, 0);
            
            if (row == 2 && col == 9) {
                lv_label_set_text(pwd_keyboard[row][col], "^");
            } else if (row == 3 && col == 8) {
                lv_label_set_text(pwd_keyboard[row][col], "<");
            } else if (row == 3 && col == 9) {
                lv_label_set_text(pwd_keyboard[row][col], ">");
            } else {
                char ch[2] = {pwd_keys[row][col], '\0'};
                lv_label_set_text(pwd_keyboard[row][col], ch);
            }
            
            lv_obj_set_style_text_color(pwd_keyboard[row][col], lv_color_hex(0x888888), 0);
            int x = 10 + col * 22;
            int y = 75 + row * 18;
            lv_obj_set_pos(pwd_keyboard[row][col], x, y);
        }
    }

    pwd_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(pwd_hint, "SET:input|Press:back");
    lv_obj_set_style_text_font(pwd_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t pwd_cbs = {
        .on_encoder_cw = pwd_on_encoder_cw,
        .on_encoder_ccw = pwd_on_encoder_ccw,
        .on_encoder_press = pwd_on_encoder_press,
        .on_encoder_long_press = pwd_on_encoder_long_press,
        .on_settings_press = pwd_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_PASSWORD_INPUT, &pwd_cbs);

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

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 10; col++) {
            bool is_letter = false;
            char c = pwd_keys[row][col];
            if (c >= 'a' && c <= 'z') is_letter = true;

            /* Update label text for letters when uppercase toggles */
            if (is_letter) {
                char ch[2] = { pwd_uppercase ? (c - 'a' + 'A') : c, '\0' };
                lv_label_set_text(pwd_keyboard[row][col], ch);
            }

            /* Color: selected = green, else default gray */
            if (row == pwd_selected_row && col == pwd_selected_col) {
                lv_obj_set_style_text_color(pwd_keyboard[row][col], lv_color_hex(0x00FF00), 0);
            } else {
                lv_obj_set_style_text_color(pwd_keyboard[row][col], lv_color_hex(0x888888), 0);
            }
        }
    }
}

void ui_screen_password_set_hint(const char *hint)
{
    if (pwd_hint == NULL) return;
    lv_label_set_text(pwd_hint, hint ? hint : "");
}

static int last_scan_count = -1;

void ui_screen_wifi_list_refresh(void)
{
    int count = wifi_service_get_scan_count();

    // Only update when scan count changes
    if (count == last_scan_count) return;
    last_scan_count = count;

    const wifi_ap_info_t *results = NULL;
    if (count > 0) {
        results = wifi_service_get_ap(0);
    }

    const char *hint = count > 0 ? "SET:select" : "Scanning...";
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

void ui_screen_password_start(const char *ssid)
{
    memset(password_buffer, 0, sizeof(password_buffer));
    password_len = 0;
    pwd_uppercase = false;
    pwd_selected_row = 0;
    pwd_selected_col = 0;
    strncpy(selected_ssid, ssid ? ssid : "", sizeof(selected_ssid) - 1);
    ui_screen_password_set_ssid(ssid);
    ui_screen_password_update_display(password_buffer, pwd_selected_col);
    ui_screen_password_set_hint("SET:input|Press:back");
}

void ui_screen_password_char_next(void)
{
    if (pwd_selected_col < 9) {
        pwd_selected_col++;
    } else if (pwd_selected_row < 3) {
        pwd_selected_row++;
        pwd_selected_col = 0;
    } else {
        pwd_selected_row = 0;
        pwd_selected_col = 0;
    }
    ui_screen_password_update_display(password_buffer, pwd_selected_col);
}

void ui_screen_password_char_prev(void)
{
    if (pwd_selected_col > 0) {
        pwd_selected_col--;
    } else if (pwd_selected_row > 0) {
        pwd_selected_row--;
        pwd_selected_col = 9;
    } else {
        pwd_selected_row = 3;
        pwd_selected_col = 9;
    }
    ui_screen_password_update_display(password_buffer, pwd_selected_col);
}

void ui_screen_password_add_char(void)
{
    if (pwd_selected_row == 2 && pwd_selected_col == 9) {
        pwd_uppercase = !pwd_uppercase;
        ui_screen_password_set_hint(pwd_uppercase ? "Uppercase" : "Lowercase");
        ui_screen_password_update_display(password_buffer, pwd_selected_col);
        return;
    }
    
    if (pwd_selected_row == 3 && pwd_selected_col == 8) {
        if (password_len > 0) {
            password_buffer[--password_len] = '\0';
            ui_screen_password_update_display(password_buffer, pwd_selected_col);
        } else {
            ui_switch_screen(UI_SCREEN_WIFI_SAVED);
        }
        return;
    }
    
    if (pwd_selected_row == 3 && pwd_selected_col == 9) {
        if (strlen(password_buffer) > 0) {
            wifi_service_connect(selected_ssid, password_buffer);
            ui_switch_screen(UI_SCREEN_WIFI_SAVED);
        }
        return;
    }
    
    if (password_len >= 63) return;
    
    char c = pwd_keys[pwd_selected_row][pwd_selected_col];
    
    if (pwd_uppercase && c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    }
    
    password_buffer[password_len++] = c;
    ui_screen_password_update_display(password_buffer, pwd_selected_col);
}

void ui_screen_password_delete_char(void)
{
    if (password_len > 0) {
        password_buffer[--password_len] = '\0';
        ui_screen_password_update_display(password_buffer, pwd_selected_col);
    }
}

void ui_screen_password_confirm(void)
{
    if (strlen(password_buffer) > 0) {
        wifi_service_connect(selected_ssid, password_buffer);
    }
}

void ui_screen_password_cancel(void)
{
    memset(password_buffer, 0, sizeof(password_buffer));
    password_len = 0;
}
