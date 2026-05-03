#include "ui_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITEM_HEIGHT 22

typedef struct {
    const ui_list_item_t *items;
    int count;
    int selected;
    int scroll;
    int visible_count;
    int list_width;
    int list_height;
    int list_x;
    int list_y;
    lv_obj_t **key_labels;
    lv_obj_t **value_labels;
    lv_obj_t *scrollbar;
    ui_list_item_cb_t click_callback;
    lv_color_t selected_color;
} ui_list_data_t;

static void update_display(lv_obj_t *list);
static ui_list_data_t *get_list_data(lv_obj_t *list);

static ui_list_data_t *get_list_data(lv_obj_t *list)
{
    return (ui_list_data_t *)lv_obj_get_user_data(list);
}

static void ensure_visible(lv_obj_t *list)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data || data->count == 0) return;
    
    if (data->selected < data->scroll) {
        data->scroll = data->selected;
    } else if (data->selected >= data->scroll + data->visible_count) {
        data->scroll = data->selected - data->visible_count + 1;
    }
    
    if (data->scroll > data->count - data->visible_count) {
        data->scroll = data->count - data->visible_count;
    }
    if (data->scroll < 0) data->scroll = 0;
    
    update_display(list);
}

static void create_labels(lv_obj_t *list, int visible_count)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data) return;
    
    for (int i = 0; i < visible_count; i++) {
        data->key_labels[i] = lv_label_create(list);
        if(data->key_labels[i]) {
            lv_obj_set_style_text_font(data->key_labels[i], &lv_font_montserrat_16, 0);
            lv_label_set_text(data->key_labels[i], "");
            lv_obj_set_pos(data->key_labels[i], 10, i * ITEM_HEIGHT);
        }
        
        data->value_labels[i] = lv_label_create(list);
        if(data->value_labels[i]) {
            lv_obj_set_style_text_font(data->value_labels[i], &lv_font_montserrat_16, 0);
            lv_label_set_text(data->value_labels[i], "");
            lv_obj_set_pos(data->value_labels[i], data->list_width - 70, i * ITEM_HEIGHT);
            lv_obj_set_style_text_align(data->value_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        }
    }
}

static void update_display(lv_obj_t *list)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data) return;
    
    for (int i = 0; i < data->visible_count; i++) {
        if (data->key_labels[i] == NULL) continue;
        
        int idx = data->scroll + i;
        if (idx < data->count && data->items) {
            lv_obj_clear_flag(data->key_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(data->value_labels[i], LV_OBJ_FLAG_HIDDEN);
            
            lv_label_set_text(data->key_labels[i], data->items[idx].key);
            lv_label_set_text(data->value_labels[i], data->items[idx].value);
            
            if (idx == data->selected) {
                lv_obj_set_style_text_color(data->key_labels[i], data->selected_color, 0);
                lv_obj_set_style_text_color(data->value_labels[i], data->selected_color, 0);
            } else {
                lv_obj_set_style_text_color(data->key_labels[i], lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_color(data->value_labels[i], lv_color_hex(0xAAAAAA), 0);
            }
        } else {
            lv_obj_add_flag(data->key_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(data->value_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    if (data->scrollbar && data->count > data->visible_count) {
        int track_height = data->visible_count * ITEM_HEIGHT;
        int scrollbar_height = (data->visible_count * track_height) / data->count;
        if (scrollbar_height < 15) scrollbar_height = 15;
        
        int max_scroll = data->count - data->visible_count;
        int scrollbar_y = (data->scroll * (track_height - scrollbar_height)) / max_scroll;
        
        lv_obj_set_size(data->scrollbar, 4, scrollbar_height);
        lv_obj_set_pos(data->scrollbar, data->list_width - 6, scrollbar_y);
        lv_obj_clear_flag(data->scrollbar, LV_OBJ_FLAG_HIDDEN);
    } else if (data->scrollbar) {
        lv_obj_add_flag(data->scrollbar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_list_cleanup(lv_event_t *event)
{
    ui_list_data_t *data = (ui_list_data_t *)lv_event_get_user_data(event);
    if (data) {
        if (data->key_labels) {
            free(data->key_labels);
            data->key_labels = NULL;
        }
        if (data->value_labels) {
            free(data->value_labels);
            data->value_labels = NULL;
        }
        free(data);
    }
}

lv_obj_t *ui_list_create(lv_obj_t *parent, int width, int height, int x, int y)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, width, height);
    lv_obj_set_pos(list, x, y);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    
    ui_list_data_t *data = (ui_list_data_t *)malloc(sizeof(ui_list_data_t));
    memset(data, 0, sizeof(ui_list_data_t));
    data->count = 0;
    data->selected = 0;
    data->scroll = 0;
    data->list_width = width;
    data->list_height = height;
    data->list_x = x;
    data->list_y = y;
    data->visible_count = height / ITEM_HEIGHT;
    data->click_callback = NULL;
    data->selected_color = lv_color_hex(0x00FF00);
    lv_obj_set_user_data(list, data);
    
    data->key_labels = (lv_obj_t **)malloc(sizeof(lv_obj_t *) * data->visible_count);
    data->value_labels = (lv_obj_t **)malloc(sizeof(lv_obj_t *) * data->visible_count);
    memset(data->key_labels, 0, sizeof(lv_obj_t *) * data->visible_count);
    memset(data->value_labels, 0, sizeof(lv_obj_t *) * data->visible_count);
    
    create_labels(list, data->visible_count);
    
    data->scrollbar = lv_obj_create(list);
    lv_obj_set_size(data->scrollbar, 4, 15);
    lv_obj_set_pos(data->scrollbar, width - 6, 0);
    lv_obj_set_style_bg_color(data->scrollbar, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_opa(data->scrollbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(data->scrollbar, 2, 0);
    lv_obj_set_style_border_width(data->scrollbar, 0, 0);
    lv_obj_add_flag(data->scrollbar, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_add_event_cb(list, (lv_event_cb_t)ui_list_cleanup, LV_EVENT_DELETE, data);
    
    return list;
}

void ui_list_set_items(lv_obj_t *list, const ui_list_item_t *items, int count)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data) return;
    
    data->items = items;
    data->count = count;
    data->selected = 0;
    data->scroll = 0;
    
    ensure_visible(list);
    update_display(list);
}

void ui_list_set_selected(lv_obj_t *list, int index)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data || index < 0 || index >= data->count) return;
    
    data->selected = index;
    ensure_visible(list);
    update_display(list);
}

int ui_list_get_selected(lv_obj_t *list)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data) return 0;
    return data->selected;
}

void ui_list_nav_next(lv_obj_t *list)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data || data->count == 0) return;
    
    data->selected = (data->selected + 1) % data->count;
    ensure_visible(list);
    update_display(list);
}

void ui_list_nav_prev(lv_obj_t *list)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data || data->count == 0) return;
    
    data->selected = (data->selected - 1 + data->count) % data->count;
    ensure_visible(list);
    update_display(list);
}

void ui_list_set_click_callback(lv_obj_t *list, ui_list_item_cb_t callback)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data) return;
    data->click_callback = callback;
}

void ui_list_set_selected_color(lv_obj_t *list, lv_color_t color)
{
    ui_list_data_t *data = get_list_data(list);
    if (!data) return;
    data->selected_color = color;
    update_display(list);
}
