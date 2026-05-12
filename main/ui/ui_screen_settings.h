#pragma once

#include "lvgl.h"

typedef enum {
    SETTINGS_MODE_IDLE = 0,
    SETTINGS_MODE_SELECT,
} settings_mode_t;

lv_obj_t* ui_screen_settings_create(void);
settings_mode_t ui_screen_settings_get_mode(void);
void ui_screen_settings_set_mode(settings_mode_t mode);
int ui_screen_settings_get_current_item(void);
