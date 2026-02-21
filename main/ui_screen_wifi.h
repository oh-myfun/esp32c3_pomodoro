#ifndef UI_SCREEN_WIFI_H
#define UI_SCREEN_WIFI_H

#include "lvgl.h"
#include "wifi_manager.h"

lv_obj_t* ui_screen_wifi_list_create(void);
void ui_screen_wifi_list_update(int count, wifi_scan_result_t *results, int selected, const char *hint);

lv_obj_t* ui_screen_password_create(void);
void ui_screen_password_set_ssid(const char *ssid);
void ui_screen_password_update_display(const char *password, int cursor_pos);
void ui_screen_password_set_hint(const char *hint);

#endif
