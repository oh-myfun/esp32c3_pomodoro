#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "lvgl.h"

// 界面ID
typedef enum {
    UI_SCREEN_MAIN = 0,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_COUNT
} ui_screen_id_t;

// 设置模式状态
typedef enum {
    SETTINGS_MODE_IDLE = 0,      // 空闲（在主界面）
    SETTINGS_MODE_SELECT,        // 选择设置项
    SETTINGS_MODE_ADJUST         // 调整设置值
} settings_mode_t;

// 设置项ID
typedef enum {
    SETTINGS_BRIGHTNESS = 0,
    SETTINGS_CONTRAST,
    SETTINGS_LANGUAGE,
    SETTINGS_COUNT
} settings_item_t;

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

// 设置项选择（滚轮）
void ui_settings_select_next(void);
void ui_settings_select_prev(void);

// 进入设置项调整
void ui_settings_enter_adjust(void);

// 设置值调整（滚轮）
void ui_settings_adjust_up(void);
void ui_settings_adjust_down(void);

// 更新主界面时间显示
void ui_update_time(void);

// 更新温度显示
void ui_update_temp(float temp);

// 更新湿度显示
void ui_update_humidity(float humidity);

#endif
