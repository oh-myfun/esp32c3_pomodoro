#pragma once

#include "lvgl.h"

typedef enum {
    SETTINGS_MODE_IDLE = 0,
    SETTINGS_MODE_SELECT,
} settings_mode_t;

lv_obj_t* ui_screen_settings_create(void);
