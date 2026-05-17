#pragma once

#include "lvgl.h"
#include "service/wifi_service.h"
#include "ui_manager.h"

lv_obj_t* ui_screen_wifi_list_create(void);
lv_obj_t* ui_screen_wifi_list_get_list(void);
void ui_screen_wifi_list_update(int count, wifi_ap_info_t *results, int selected, const char *hint);
void ui_screen_wifi_list_refresh(void);
int ui_screen_wifi_list_get_selected(void);
