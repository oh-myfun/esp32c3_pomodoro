#pragma once

#include "lvgl.h"

lv_obj_t *ui_screen_settings_buddy_create(void);
void ui_screen_settings_buddy_refresh(void);
void ui_screen_settings_buddy_set_scan_result(const char *host, int port, const char *pairing_code);
