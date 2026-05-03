#pragma once

#include "lvgl.h"

typedef struct {
    const char *key;
    const char *value;
} ui_list_item_t;

typedef void (*ui_list_item_cb_t)(int index);

lv_obj_t *ui_list_create(lv_obj_t *parent, int width, int height, int x, int y);
void ui_list_set_items(lv_obj_t *list, const ui_list_item_t *items, int count);
void ui_list_set_selected(lv_obj_t *list, int index);
int ui_list_get_selected(lv_obj_t *list);
void ui_list_nav_next(lv_obj_t *list);
void ui_list_nav_prev(lv_obj_t *list);
void ui_list_set_click_callback(lv_obj_t *list, ui_list_item_cb_t callback);
void ui_list_set_selected_color(lv_obj_t *list, lv_color_t color);
