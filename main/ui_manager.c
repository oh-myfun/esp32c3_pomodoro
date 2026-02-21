#include "ui_manager.h"
#include "wifi_manager.h"
#include "ui_screen_main.h"
#include "ui_screen_pomodoro.h"
#include "ui_screen_settings.h"
#include "ui_screen_wifi.h"
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
static lv_obj_t *pwd_hint = NULL;
static lv_obj_t *pwd_keyboard_labels[4][10];  // 键盘字符label
static char password_buffer[64] = {0};
static int password_len = 0;

// 密码键盘布局：4行x10列
// 行0: 数字0-9
// 行1: 字母a-j
// 行2: 字母k-t  
// 行3: 字母u-z + Ab + OK
static const char pwd_keyboard_lower[4][10] = {
    "0123456789",
    "abcdefghij",
    "klmnopqrst",
    "uvwxyz"
};
static const char pwd_keyboard_upper[4][10] = {
    "0123456789",
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ"
};
static const int pwd_keyboard_cols = 10;
static const int pwd_keyboard_rows = 4;
static bool pwd_uppercase = false;

static int pwd_selected_row = 0;
static int pwd_selected_col = 0;

// 前向声明
static void update_password_display(void);

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

void ui_init(void)
{
    screens[UI_SCREEN_MAIN] = ui_screen_main_create();
    screens[UI_SCREEN_SETTINGS] = ui_screen_settings_create();
    screens[UI_SCREEN_WIFI_LIST] = ui_screen_wifi_list_create();
    screens[UI_SCREEN_PASSWORD_INPUT] = ui_screen_password_create();

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
    bool scan_done = (count > 0);

    for (int i = 0; i < 8; i++) {
        int idx = wifi_list_scroll + i;
        if (scan_done && idx < count) {
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

    if (scan_done && count > 0) {
        char hint[48];
        snprintf(hint, sizeof(hint), "%d/%d SET:Conn ENC:Back", wifi_list_selected + 1, count);
        lv_label_set_text(wifi_list_hint, hint);
    } else {
        lv_label_set_text(wifi_list_hint, "Scanning...");
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

void ui_update_wifi_status_ex(const char *status, uint32_t color)
{
    if (wifi_status_label) {
        lv_label_set_text(wifi_status_label, status);
        lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(color), 0);
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
    pwd_uppercase = false;

    lv_label_set_text(pwd_ssid_label, ssid);
    lv_label_set_text(pwd_display, "");
    lv_label_set_text(pwd_hint, "Roll:chg  SET:add  ENC:del");

    ui_switch_screen(UI_SCREEN_PASSWORD_INPUT);

    // 初始化键盘显示
    update_password_display();
}

static void update_password_display(void)
{
    // 重新绘制键盘以反映大小写状态
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 10; col++) {
            if (pwd_keyboard_labels[row][col]) {
                const char *row_str = pwd_uppercase ? pwd_keyboard_upper[row] : pwd_keyboard_lower[row];
                char buf[4] = {0};
                
                if (row < 3) {
                    buf[0] = row_str[col];
                } else {
                    if (col < 6) {
                        buf[0] = row_str[col];
                    } else if (col == 6) {
                        snprintf(buf, sizeof(buf), "Ab");
                    } else if (col == 7) {
                        snprintf(buf, sizeof(buf), "OK");
                    } else {
                        // 隐藏第8、9列
                        snprintf(buf, sizeof(buf), " ");
                    }
                }
                lv_label_set_text(pwd_keyboard_labels[row][col], buf);
            }
        }
    }

    // 取消所有字符的高亮
    for (int row = 0; row < 4; row++) {
        int max_col = (row == 3) ? 8 : pwd_keyboard_cols;
        for (int col = 0; col < max_col; col++) {
            if (pwd_keyboard_labels[row][col]) {
                lv_obj_set_style_text_color(pwd_keyboard_labels[row][col], lv_color_hex(0x666666), 0);
            }
        }
    }

    // 高亮当前选中的字符
    if (pwd_keyboard_labels[pwd_selected_row][pwd_selected_col]) {
        lv_obj_set_style_text_color(pwd_keyboard_labels[pwd_selected_row][pwd_selected_col], lv_color_hex(0x00FF00), 0);
    }
}

void ui_password_input_char_next(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;

    pwd_selected_col++;
    
    // 检查当前行的有效列数
    int max_col = (pwd_selected_row == 3) ? 8 : pwd_keyboard_cols;
    
    if (pwd_selected_col >= max_col) {
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
        pwd_selected_row--;
        if (pwd_selected_row < 0) {
            pwd_selected_row = pwd_keyboard_rows - 1;
        }
        pwd_selected_col = (pwd_selected_row == 3) ? 7 : (pwd_keyboard_cols - 1);
    }
    update_password_display();
}

void ui_password_input_add_char(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;

    // 检查是否选中大小写切换（第3行第6列）
    if (pwd_selected_row == 3 && pwd_selected_col == 6) {
        pwd_uppercase = !pwd_uppercase;
        update_password_display();
        return;
    }

    // 检查是否选中OK（第3行第7列）
    if (pwd_selected_row == 3 && pwd_selected_col == 7) {
        ui_password_input_confirm();
        return;
    }

    const char *row_str = pwd_uppercase ? pwd_keyboard_upper[pwd_selected_row] : pwd_keyboard_lower[pwd_selected_row];
    char ch = row_str[pwd_selected_col];
    
    if (password_len >= sizeof(password_buffer) - 1) return;

    password_buffer[password_len++] = ch;
    password_buffer[password_len] = '\0';

    // 明文显示密码
    lv_label_set_text(pwd_display, password_buffer);
}

void ui_password_input_delete_char(void)
{
    if (current_screen != UI_SCREEN_PASSWORD_INPUT) return;
    if (password_len <= 0) return;

    password_buffer[--password_len] = '\0';

    // 明文显示密码
    lv_label_set_text(pwd_display, password_buffer);
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
    ui_screen_main_update_time();
}

void ui_update_temp(float temp)
{
    ui_screen_main_update_temp(temp);
}

void ui_update_humidity(float humidity)
{
    ui_screen_main_update_humidity(humidity);
}

void ui_pomodoro_update_time(uint32_t remaining_seconds)
{
    ui_screen_pomodoro_update_time(remaining_seconds);
}

void ui_pomodoro_update_phase(const char *phase)
{
    ui_screen_pomodoro_update_phase(phase);
}

void ui_pomodoro_update_completed(uint32_t count)
{
    ui_screen_pomodoro_update_completed(count);
}

void ui_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed)
{
    ui_screen_pomodoro_update_state(phase, remaining_seconds, completed);
}
