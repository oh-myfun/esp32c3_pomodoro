#pragma once

#include "lvgl.h"

// 界面ID
typedef enum {
    UI_SCREEN_MAIN = 0,
    UI_SCREEN_POMODORO,
    UI_SCREEN_CHAT,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_SETTINGS_POMODORO,
    UI_SCREEN_WIFI_LIST,
    UI_SCREEN_PASSWORD_INPUT,
    UI_SCREEN_COUNT
} ui_screen_id_t;

// 设置模式状态
typedef enum {
    SETTINGS_MODE_IDLE = 0,
    SETTINGS_MODE_SELECT,
    SETTINGS_MODE_ADJUST
} settings_mode_t;

// 设置项ID
typedef enum {
    SETTINGS_BRIGHTNESS = 0,
    SETTINGS_CONTRAST,
    SETTINGS_LANGUAGE,
    SETTINGS_POMODORO,
    SETTINGS_WIFI,
    SETTINGS_COUNT
} settings_item_t;

// 番茄钟操作
typedef enum {
    POMODORO_OP_NONE = 0,
    POMODORO_OP_START,
    POMODORO_OP_PAUSE,
    POMODORO_OP_RESUME,
    POMODORO_OP_STOP,
    POMODORO_OP_RESET
} pomodoro_op_t;

// 输入回调接口
typedef struct {
    void (*on_encoder_cw)(void);
    void (*on_encoder_ccw)(void);
    void (*on_encoder_press)(void);
    void (*on_encoder_long_press)(void);
    void (*on_settings_press)(void);
} ui_input_callbacks_t;

// 注册/注销输入回调
void ui_register_input_callbacks(ui_screen_id_t screen, const ui_input_callbacks_t *cbs);
void ui_unregister_input_callbacks(ui_screen_id_t screen);

// 分发输入事件
void ui_dispatch_encoder_cw(void);
void ui_dispatch_encoder_ccw(void);
void ui_dispatch_encoder_press(void);
void ui_dispatch_encoder_long_press(void);
void ui_dispatch_settings_press(void);

// 初始化UI系统
void ui_init(void);

// 切换到指定界面
void ui_switch_screen(ui_screen_id_t screen_id);

// 获取当前界面ID
ui_screen_id_t ui_get_current_screen(void);

// 获取当前设置模式
settings_mode_t ui_get_settings_mode(void);

// 进入设置模式
void ui_enter_settings(void);

// 退出设置模式
void ui_exit_settings(void);

// 设置项选择
void ui_settings_select_next(void);
void ui_settings_select_prev(void);

// 进入设置项调整
void ui_settings_enter_adjust(void);

// 设置值调整
void ui_settings_adjust_up(void);
void ui_settings_adjust_down(void);

// WiFi列表相关
void ui_update_wifi_status(const char *status);
void ui_update_wifi_status_ex(const char *status, uint32_t color);

// 密码输入相关
void ui_password_input_start(const char *ssid);
void ui_password_input_char_next(void);
void ui_password_input_char_prev(void);
void ui_password_input_add_char(void);
void ui_password_input_delete_char(void);
void ui_password_input_confirm(void);
void ui_password_input_cancel(void);

// 更新显示
void ui_update_time(void);
void ui_update_temp(float temp);
void ui_update_humidity(float humidity);

// 番茄钟界面
void ui_pomodoro_update_time(uint32_t remaining_seconds);
void ui_pomodoro_update_phase(const char *phase);
void ui_pomodoro_update_completed(uint32_t count);
void ui_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed);
