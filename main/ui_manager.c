#include "ui_manager.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI";

static lv_obj_t *screens[UI_SCREEN_COUNT];
static ui_screen_id_t current_screen = UI_SCREEN_MAIN;
static settings_mode_t settings_mode = SETTINGS_MODE_IDLE;

// 主界面控件
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *humidity_label = NULL;
static lv_obj_t *wifi_status_label = NULL;

// 设置界面控件
static lv_obj_t *settings_title = NULL;
static lv_obj_t *settings_item_labels[SETTINGS_COUNT];
static lv_obj_t *settings_value_labels[SETTINGS_COUNT];
static lv_obj_t *settings_hint = NULL;

// WiFi列表界面控件
static lv_obj_t *wifi_list_title = NULL;
static lv_obj_t *wifi_list_labels[8];
static lv_obj_t *wifi_list_hint = NULL;
static int wifi_list_selected = 0;
static int wifi_list_scroll = 0;
static char selected_ssid[33] = {0};

// 密码输入界面控件
static lv_obj_t *pwd_title = NULL;
static lv_obj_t *pwd_ssid_label = NULL;
static lv_obj_t *pwd_display = NULL;
static lv_obj_t *pwd_char_display = NULL;
static lv_obj_t *pwd_hint = NULL;
static char password_buffer[64] = {0};
static int password_len = 0;

// 密码键盘布局：4行x10列
// 行0: 数字0-9
// 行1: 字母a-j
// 行2: 字母k-t  
// 行3: 字母u-z + 连接
static const char *pwd_keyboard[4] = {
    "0123456789",    // 10个
    "abcdefghij",    // 10个
    "klmnopqrst",    // 10个
    "uvwxyz____"     // 10个，_表示空
};
static const int pwd_keyboard_cols = 10;
static const int pwd_keyboard_rows = 4;

static int pwd_selected_row = 0;
static int pwd_selected_col = 0;

// 设置项当前值
static int settings_values[SETTINGS_COUNT] = {50, 50, 0};
static int current_settings_item = 0;

// 设置项名称
static const char *settings_names[SETTINGS_COUNT] = {
    "Brightness",
    "Contrast",
    "Language",
    "WiFi"
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

        char buf[20];
        switch (i) {
            case SETTINGS_LANGUAGE:
                sprintf(buf, "%s", language_options[settings_values[i] % language_count]);
                break;
            case SETTINGS_WIFI:
                sprintf(buf, ">");
                break;
            default:
                sprintf(buf, "%d", settings_values[i]);
                break;
        }
        lv_label_set_text(settings_value_labels[i], buf);
    }

    if (settings_mode == SETTINGS_MODE_SELECT) {
        lv_label_set_text(settings_hint, "Press SET to adjust");
    } else if (settings_mode == SETTINGS_MODE_ADJUST) {
        if (current_settings_item == SETTINGS_WIFI) {
            lv_label_set_text(settings_hint, "Press SET to scan");
        } else {
            lv_label_set_text(settings_hint, "Adjusting...");
        }
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

    wifi_status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(wifi_status_label, "");
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 28);

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

// WiFi列表界面
lv_obj_t *ui_create_wifi_list_screen(void)
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
        lv_obj_align(wifi_list_labels[i], LV_ALIGN_TOP_LEFT, 10, 35 + i * 24);
    }

    wifi_list_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(wifi_list_hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(wifi_list_hint, "Scanning...");
    lv_obj_set_style_text_font(wifi_list_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_list_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return screen;
}

// 密码输入界面
lv_obj_t *ui_create_password_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    pwd_title = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(pwd_title, "Password");
    lv_obj_set_style_text_font(pwd_title, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_title, LV_ALIGN_TOP_MID, 0, 10);

    pwd_ssid_label = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_ssid_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(pwd_ssid_label, "");
    lv_obj_set_style_text_font(pwd_ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_ssid_label, LV_ALIGN_TOP_MID, 0, 30);

    pwd_display = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_display, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(pwd_display, "");
    lv_obj_set_style_text_font(pwd_display, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_display, LV_ALIGN_CENTER, 0, -30);

    pwd_char_display = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_char_display, lv_color_hex(0xFFFF00), 0);
    lv_label_set_text(pwd_char_display, "0");
    lv_obj_set_style_text_font(pwd_char_display, &lv_font_montserrat_28, 0);
    lv_obj_align(pwd_char_display, LV_ALIGN_CENTER, 0, 20);

    pwd_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(pwd_hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(pwd_hint, "Roll:chg  SET:add  ENC:del");
    lv_obj_set_style_text_font(pwd_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return screen;
}

void ui_init(void)
{
    screens[UI_SCREEN_MAIN] = ui_create_main_screen();
    screens[UI_SCREEN_SETTINGS] = ui_create_settings_screen();
    screens[UI_SCREEN_WIFI_LIST] = ui_create_wifi_list_screen();
    screens[UI_SCREEN_PASSWORD_INPUT] = ui_create_password_screen();

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

    // 切换到设置界面时更新显示
    if (screen_id == UI_SCREEN_SETTINGS) {
        update_settings_display();
    }
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
        if (current_settings_item == SETTINGS_WIFI) {
            wifi_manager_scan_start();
            ui_switch_screen(UI_SCREEN_WIFI_LIST);
            lv_label_set_text(wifi_list_hint, "Scanning...");
        } else {
            settings_mode = SETTINGS_MODE_ADJUST;
            update_settings_display();
            ESP_LOGI(TAG, "Enter adjust mode: %s", settings_names[current_settings_item]);
        }
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

// WiFi列表相关函数
void ui_wifi_list_refresh(void)
{
    if (current_screen != UI_SCREEN_WIFI_LIST) return;

    int count = wifi_manager_get_scan_count();

    for (int i = 0; i < 8; i++) {
        int idx = wifi_list_scroll + i;
        if (idx < count) {
            wifi_scan_result_t *ap = wifi_manager_get_scan_result(idx);
            if (ap) {
                char buf[40];
                snprintf(buf, sizeof(buf), "%s %s", ap->ssid, ap->open ? "[OPEN]" : "");
                lv_label_set_text(wifi_list_labels[i], buf);

                if (idx == wifi_list_selected) {
                    lv_obj_set_style_text_color(wifi_list_labels[i], lv_color_hex(0x00FF00), 0);
                } else {
                    lv_obj_set_style_text_color(wifi_list_labels[i], lv_color_hex(0xFFFFFF), 0);
                }
            }
        } else {
            lv_label_set_text(wifi_list_labels[i], "");
        }
    }

    if (count > 0) {
        char hint[48];
        snprintf(hint, sizeof(hint), "%d/%d SET:Conn ENC:Back", wifi_list_selected + 1, count);
        lv_label_set_text(wifi_list_hint, hint);
    } else {
        lv_label_set_text(wifi_list_hint, "No networks");
    }
}

void ui_wifi_list_select_next(void)
{
    if (current_screen != UI_SCREEN_WIFI_LIST) return;

    int count = wifi_manager_get_scan_count();
    if (count == 0) return;

    wifi_list_selected++;
    if (wifi_list_selected >= count) {
        wifi_list_selected = 0;
        wifi_list_scroll = 0;
    }

    // 自动滚动
    if (wifi_list_selected >= wifi_list_scroll + 8) {
        wifi_list_scroll = wifi_list_selected - 7;
    }

    ui_wifi_list_refresh();
}

void ui_wifi_list_select_prev(void)
{
    if (current_screen != UI_SCREEN_WIFI_LIST) return;

    int count = wifi_manager_get_scan_count();
    if (count == 0) return;

    wifi_list_selected--;
    if (wifi_list_selected < 0) {
        wifi_list_selected = count - 1;
        wifi_list_scroll = (count > 8) ? count - 8 : 0;
    }

    // 自动滚动
    if (wifi_list_selected < wifi_list_scroll) {
        wifi_list_scroll = wifi_list_selected;
    }

    ui_wifi_list_refresh();
}

void ui_wifi_list_confirm(void)
{
    if (current_screen != UI_SCREEN_WIFI_LIST) return;

    wifi_scan_result_t *ap = wifi_manager_get_scan_result(wifi_list_selected);
    if (ap) {
        strncpy(selected_ssid, ap->ssid, sizeof(selected_ssid) - 1);
        if (ap->open) {
            wifi_manager_connect(selected_ssid, "");
            settings_mode = SETTINGS_MODE_IDLE;
            ui_switch_screen(UI_SCREEN_MAIN);
            ui_update_wifi_status("Connecting...");
        } else {
            ui_password_input_start(selected_ssid);
        }
    }
}

void ui_update_wifi_status(const char *status)
{
    if (wifi_status_label) {
        lv_label_set_text(wifi_status_label, status);
    }
}

// 密码输入相关函数
void ui_password_input_start(const char *ssid)
{
    password_len = 0;
    password_buffer[0] = '\0';
    pwd_selected_row = 0;
    pwd_selected_col = 0;

    lv_label_set_text(pwd_ssid_label, ssid);
    lv_label_set_text(pwd_display, "");
    lv_label_set_text(pwd_char_display, "0");
    lv_label_set_text(pwd_hint, "Roll:chg  SET:add  ENC:del");

    ui_switch_screen(UI_SCREEN_PASSWORD_INPUT);
}

static void update_password_display(void)
{
    char ch = pwd_keyboard[pwd_selected_row][pwd_selected_col];
    char buf[8];
    if (ch == '_') {
        snprintf(buf, sizeof(buf), "[OK]");
    } else {
        snprintf(buf, sizeof(buf), "%c", ch);
    }
    lv_label_set_text(pwd_char_display, buf);
}

void ui_password_input_char_next(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;

    pwd_selected_col++;
    if (pwd_selected_col >= pwd_keyboard_cols) {
        pwd_selected_col = 0;
        pwd_selected_row++;
        if (pwd_selected_row >= pwd_keyboard_rows) {
            pwd_selected_row = 0;
        }
    }
    update_password_display();
}

void ui_password_input_char_prev(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;

    pwd_selected_col--;
    if (pwd_selected_col < 0) {
        pwd_selected_col = pwd_keyboard_cols - 1;
        pwd_selected_row--;
        if (pwd_selected_row < 0) {
            pwd_selected_row = pwd_keyboard_rows - 1;
        }
    }
    update_password_display();
}

void ui_password_input_add_char(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;

    char ch = pwd_keyboard[pwd_selected_row][pwd_selected_col];
    
    // 如果选中_[OK]，直接连接
    if (ch == '_') {
        ui_password_input_confirm();
        return;
    }

    if (password_len >= sizeof(password_buffer) - 1) return;

    password_buffer[password_len++] = ch;
    password_buffer[password_len] = '\0';

    char display[65];
    memset(display, '*', password_len);
    display[password_len] = '_';
    display[password_len + 1] = '\0';
    lv_label_set_text(pwd_display, display);
}

void ui_password_input_delete_char(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;
    if (password_len <= 0) return;

    password_buffer[--password_len] = '\0';

    char display[65];
    memset(display, '*', password_len);
    display[password_len] = '_';
    display[password_len + 1] = '\0';
    lv_label_set_text(pwd_display, display);
}

void ui_password_input_confirm(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;

    wifi_manager_connect(selected_ssid, password_buffer);
    settings_mode = SETTINGS_MODE_IDLE;
    ui_switch_screen(UI_SCREEN_MAIN);
    ui_update_wifi_status("Connecting...");
}

void ui_password_input_cancel(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;

    settings_mode = SETTINGS_MODE_SELECT;
    ui_switch_screen(UI_SCREEN_WIFI_LIST);
    ui_wifi_list_refresh();
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
