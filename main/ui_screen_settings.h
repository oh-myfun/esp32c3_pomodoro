#ifndef UI_SCREEN_SETTINGS_H
#define UI_SCREEN_SETTINGS_H

#include "lvgl.h"
#include "ui_manager.h"

lv_obj_t* ui_screen_settings_create(void);
void ui_screen_settings_update(int *values, int selected, settings_mode_t mode);
void ui_screen_settings_set_hint(const char *hint);

#endif
