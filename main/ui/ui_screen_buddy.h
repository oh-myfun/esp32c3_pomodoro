#pragma once

#include "lvgl.h"
#include <stdbool.h>

lv_obj_t* ui_screen_buddy_create(void);
void ui_screen_buddy_update_state(void);
void ui_screen_buddy_show_request(const char *tool, const char *command,
                                   const char *description, const char *hint,
                                   int option_count, int req_type,
                                   const char *option_labels[], int option_count_labels,
                                   const char *option_descs[],
                                   bool has_suggestions);
void ui_screen_buddy_clear_request(void);
void ui_screen_buddy_set_connected(bool connected);
