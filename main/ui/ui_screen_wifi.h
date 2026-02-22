#ifndef UI_SCREEN_WIFI_H
#define UI_SCREEN_WIFI_H

#include "lvgl.h"
#include "wifi_manager.h"
#include "ui_manager.h"

lv_obj_t* ui_screen_wifi_list_create(void);
lv_obj_t* ui_screen_wifi_list_get_list(void);
void ui_screen_wifi_list_update(int count, wifi_scan_result_t *results, int selected, const char *hint);
void ui_screen_wifi_list_refresh(void);
int ui_screen_wifi_list_get_selected(void);
const char* ui_screen_wifi_list_get_selected_ssid(void);

lv_obj_t* ui_screen_password_create(void);
void ui_screen_password_set_ssid(const char *ssid);
void ui_screen_password_update_display(const char *password, int cursor_pos);
void ui_screen_password_set_hint(const char *hint);
void ui_screen_password_start(const char *ssid);
void ui_screen_password_char_next(void);
void ui_screen_password_char_prev(void);
void ui_screen_password_add_char(void);
void ui_screen_password_delete_char(void);
void ui_screen_password_confirm(void);
void ui_screen_password_cancel(void);

#endif
