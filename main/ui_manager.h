#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "lvgl.h"

// 界面ID
typedef enum {
    UI_SCREEN_MAIN = 0,     // 主界面（时间）
    UI_SCREEN_TEMP,         // 温度界面
    UI_SCREEN_HUMIDITY,     // 湿度界面
    UI_SCREEN_SETTINGS,     // 设置界面
    UI_SCREEN_COUNT         // 界面总数
} ui_screen_id_t;

// 初始化UI系统
void ui_init(void);

// 切换到指定界面
void ui_switch_screen(ui_screen_id_t screen_id);

// 获取当前界面ID
ui_screen_id_t ui_get_current_screen(void);

// 切换到上一个界面
void ui_prev_screen(void);

// 切换到下一个界面
void ui_next_screen(void);

// 更新主界面时间显示（由定时器调用）
void ui_update_time(void);

// 各个界面的创建函数
lv_obj_t *ui_create_main_screen(void);
lv_obj_t *ui_create_temp_screen(void);
lv_obj_t *ui_create_humidity_screen(void);
lv_obj_t *ui_create_settings_screen(void);

#endif
