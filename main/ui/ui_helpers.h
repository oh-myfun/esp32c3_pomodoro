#pragma once

#include "lvgl.h"
#include "ui_manager.h"

/* 创建标准屏幕：纯黑背景 (UI_COLOR_BG) + 240x240 尺寸 */
lv_obj_t *ui_create_screen(void);

/* 在屏幕顶部创建标题标签（白字、custom_font_16、TOP_MID 偏移 UI_TITLE_Y_OFFSET） */
lv_obj_t *ui_create_title_label(lv_obj_t *parent, const char *text);

/* 在屏幕底部创建提示标签（灰字、custom_font_14、BOTTOM_MID 偏移 UI_HINT_BOTTOM_OFFSET） */
lv_obj_t *ui_create_hint_label(lv_obj_t *parent, const char *text);

/* 创建并注册 input callbacks（NULL 字段会被自动忽略） */
void ui_register_callbacks(ui_screen_id_t screen,
                           void (*on_cw)(void),
                           void (*on_ccw)(void),
                           void (*on_press)(void),
                           void (*on_long_press)(void),
                           void (*on_settings_press)(void),
                           void (*on_settings_long_press)(void),
                           const char *(*on_long_press_hint)(bool));
