#pragma once

#include "lvgl.h"
#include <time.h>
#include <sys/time.h>

lv_obj_t* ui_screen_main_create(void);
void ui_screen_main_update_time(void);
void ui_screen_main_update_wifi_status(const char *status, uint32_t color);
