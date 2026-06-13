#pragma once

#include "lvgl.h"

lv_obj_t *ui_screen_settings_sound_demo_create(void);

/* 由 ui_update_task 每 100ms 调用：buzzer 播放结束时清除 ▶ 标记 */
void ui_screen_settings_sound_demo_update_play_state(void);
