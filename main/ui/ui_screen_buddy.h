#pragma once

#include "lvgl.h"
#include <stdbool.h>

lv_obj_t* ui_screen_buddy_create(void);
void ui_screen_buddy_update_state(void);
void ui_screen_buddy_show_prompt(const char *tool, const char *hint);
void ui_screen_buddy_clear_prompt(void);
void ui_screen_buddy_set_connected(bool connected);
