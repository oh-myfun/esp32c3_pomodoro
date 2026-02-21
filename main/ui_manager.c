#include "ui_manager.h"
#include "wifi_manager.h"
#include "ui_screen_main.h"
#include "ui_screen_pomodoro.h"
#include "ui_screen_settings.h"
#include "ui_screen_wifi.h"
#include "esp_log.h"
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
    ui_screen_settings_enter();
    ui_switch_screen(UI_SCREEN_SETTINGS);
    ui_screen_settings_update(ui_screen_settings_get_values(), ui_screen_settings_get_current_item(), ui_screen_settings_get_mode());
}

void ui_exit_settings(void)
{
    ui_screen_settings_exit();
    if (ui_screen_settings_get_mode() == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_MAIN);
    }
}

void ui_settings_select_next(void)
{
    ui_screen_settings_select_next();
}

void ui_settings_select_prev(void)
{
    ui_screen_settings_select_prev();
}

void ui_settings_enter_adjust(void)
{
    ui_screen_settings_enter_adjust();
    ui_screen_settings_update(ui_screen_settings_get_values(), ui_screen_settings_get_current_item(), ui_screen_settings_get_mode());
}

void ui_settings_adjust_up(void)
{
    ui_screen_settings_adjust_up();
}

void ui_settings_adjust_down(void)
{
    ui_screen_settings_adjust_down();
}

// WiFi列表相关函数
void ui_wifi_list_refresh(void)
{
    ui_screen_wifi_list_refresh();
}

void ui_wifi_list_select_next(void)
{
    ui_screen_wifi_list_select_next();
}

void ui_wifi_list_select_prev(void)
{
    ui_screen_wifi_list_select_prev();
}

void ui_wifi_list_confirm(void)
{
    ui_screen_wifi_list_confirm();
}

void ui_update_wifi_status_ex(const char *status, uint32_t color)
{
    ui_screen_main_update_wifi_status(status, color);
}

void ui_update_wifi_status(const char *status)
{
    ui_screen_main_update_wifi_status(status, 0xFFFFFF);
}

// 密码输入相关函数
void ui_password_input_start(const char *ssid)
{
    ui_screen_password_start(ssid);
    ui_switch_screen(UI_SCREEN_PASSWORD_INPUT);
}

void ui_password_input_char_next(void)
{
    ui_screen_password_char_next();
}

void ui_password_input_char_prev(void)
{
    ui_screen_password_char_prev();
}

void ui_password_input_add_char(void)
{
    ui_screen_password_add_char();
}

void ui_password_input_delete_char(void)
{
    ui_screen_password_delete_char();
}

void ui_password_input_confirm(void)
{
    ui_screen_password_confirm();
    ui_switch_screen(UI_SCREEN_MAIN);
}

void ui_password_input_cancel(void)
{
    ui_screen_password_cancel();
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
