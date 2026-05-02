#pragma once

#include "lvgl.h"

typedef enum {
    SETTINGS_MODE_IDLE = 0,
    SETTINGS_MODE_SELECT,
    SETTINGS_MODE_ADJUST
} settings_mode_t;

lv_obj_t* ui_screen_settings_create(void);
void ui_screen_settings_update(int *values, int selected, settings_mode_t mode);
void ui_screen_settings_set_hint(const char *hint);

void ui_screen_settings_enter(void);
void ui_screen_settings_exit(void);
settings_mode_t ui_screen_settings_get_mode(void);
void ui_screen_settings_set_mode(settings_mode_t mode);
void ui_screen_settings_select_next(void);
void ui_screen_settings_select_prev(void);
void ui_screen_settings_enter_adjust(void);
void ui_screen_settings_adjust_up(void);
void ui_screen_settings_adjust_down(void);
int ui_screen_settings_get_current_item(void);
int* ui_screen_settings_get_values(void);
