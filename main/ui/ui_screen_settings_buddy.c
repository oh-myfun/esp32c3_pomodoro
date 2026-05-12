#include "ui_screen_settings_buddy.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "buddy/buddy.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_BUDDY";

typedef enum {
    BUDDY_MODE_NAV = 0,
    BUDDY_MODE_ADJUST,
} buddy_edit_mode_t;

#define BUDDY_ITEM_COUNT 1

static buddy_edit_mode_t buddy_mode = BUDDY_MODE_NAV;
static int buddy_selected_item = 0;
static int buddy_species_index = 0;

static lv_obj_t *screen = NULL;
static lv_obj_t *buddy_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[BUDDY_ITEM_COUNT][20];
static char item_values[BUDDY_ITEM_COUNT][16];
static ui_list_item_t items[BUDDY_ITEM_COUNT];

static void update_display(void)
{
    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_SPECIES));
    snprintf(item_values[0], sizeof(item_values[0]), "%s",
             buddy_get_species_name(buddy_species_index));

    for (int i = 0; i < BUDDY_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (buddy_list) {
        lv_color_t color;
        if (buddy_mode == BUDDY_MODE_ADJUST) {
            color = lv_color_hex(0xFFFF00);
        } else {
            color = lv_color_hex(0x00FF00);
        }
        ui_list_set_selected_color(buddy_list, color);
        ui_list_set_items(buddy_list, items, BUDDY_ITEM_COUNT);
        ui_list_set_selected(buddy_list, buddy_selected_item);
    }

    if (hint_label) {
        if (buddy_mode == BUDDY_MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
        }
    }
}

static void buddy_on_encoder_cw(void)
{
    if (buddy_mode == BUDDY_MODE_NAV) {
        buddy_selected_item = (buddy_selected_item + 1) % BUDDY_ITEM_COUNT;
        update_display();
    } else {
        int count = buddy_get_species_count();
        buddy_species_index = (buddy_species_index + 1) % count;
        update_display();
    }
}

static void buddy_on_encoder_ccw(void)
{
    if (buddy_mode == BUDDY_MODE_NAV) {
        buddy_selected_item = (buddy_selected_item - 1 + BUDDY_ITEM_COUNT) % BUDDY_ITEM_COUNT;
        update_display();
    } else {
        int count = buddy_get_species_count();
        buddy_species_index = (buddy_species_index - 1 + count) % count;
        update_display();
    }
}

static void buddy_on_encoder_press(void)
{
    if (buddy_mode == BUDDY_MODE_ADJUST) {
        buddy_mode = BUDDY_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

static void buddy_on_settings_press(void)
{
    if (buddy_mode == BUDDY_MODE_NAV) {
        buddy_mode = BUDDY_MODE_ADJUST;
        update_display();
    } else {
        buddy_set_species(buddy_species_index);
        buddy_mode = BUDDY_MODE_NAV;
        update_display();
    }
}

static void buddy_on_encoder_long_press(void)
{
    if (buddy_mode == BUDDY_MODE_ADJUST) {
        buddy_mode = BUDDY_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

lv_obj_t* ui_screen_settings_buddy_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    buddy_list = NULL;
    hint_label = NULL;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_T_BUDDY));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    buddy_list = ui_list_create(screen, 220, 196, 10, 30);

    buddy_info_t info = buddy_get_info();
    buddy_species_index = info.species_index;
    buddy_mode = BUDDY_MODE_NAV;
    buddy_selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = buddy_on_encoder_cw,
        .on_encoder_ccw = buddy_on_encoder_ccw,
        .on_encoder_press = buddy_on_encoder_press,
        .on_encoder_long_press = buddy_on_encoder_long_press,
        .on_settings_press = buddy_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_BUDDY, &cbs);

    ESP_LOGI(TAG, "Settings Buddy screen created");
    return screen;
}

void ui_screen_settings_buddy_refresh(void)
{
    buddy_info_t info = buddy_get_info();
    buddy_species_index = info.species_index;
    update_display();
}
