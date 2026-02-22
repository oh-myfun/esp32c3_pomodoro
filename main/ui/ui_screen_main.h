#ifndef UI_SCREEN_MAIN_H
#define UI_SCREEN_MAIN_H

#include "lvgl.h"
#include <time.h>
#include <sys/time.h>

lv_obj_t* ui_screen_main_create(void);
void ui_screen_main_update_time(void);
void ui_screen_main_update_temp(float temp);
void ui_screen_main_update_humidity(float humidity);
void ui_screen_main_update_wifi_status(const char *status, uint32_t color);

#endif
