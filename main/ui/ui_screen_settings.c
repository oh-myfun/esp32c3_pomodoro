#include "ui_screen_settings.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS";

#define SETTINGS_ITEM_COUNT 6

static lv_obj_t *settings_title = NULL;
static lv_obj_t *settings_list = NULL;
static lv_obj_t *settings_hint = NULL;

static settings_mode_t settings_mode = SETTINGS_MODE_IDLE;
static int current_settings_item = 0;

static char item_keys[SETTINGS_ITEM_COUNT][20];
static char item_values[SETTINGS_ITEM_COUNT][4];
static ui_list_item_t items[SETTINGS_ITEM_COUNT];

static const char *settings_names[SETTINGS_ITEM_COUNT] = {
    "Pomodoro", "Buddy", "Light", "WiFi", "Time", "System"
};

static void update_display(void)
{
    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        strncpy(item_keys[i], settings_names[i], sizeof(item_keys[i]) - 1);
        snprintf(item_values[i], sizeof(item_values[i]), ">");
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (settings_list) {
        lv_color_t color;
        if (settings_mode == SETTINGS_MODE_SELECT) {
            color = lv_color_hex(0x00FF00);
        } else {
            color = lv_color_hex(0xFFFFFF);
        }
        ui_list_set_selected_color(settings_list, color);
        ui_list_set_items(settings_list, items, SETTINGS_ITEM_COUNT);
        ui_list_set_selected(settings_list, current_settings_item);
    }

    if (settings_hint) {
        if (settings_mode == SETTINGS_MODE_SELECT) {
            lv_label_set_text(settings_hint, "SET:enter|Press:back");
        } else {
            lv_label_set_text(settings_hint, "SET:enter");
        }
    }
}

static void settings_on_encoder_cw(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_MAIN);
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        current_settings_item = (current_settings_item + 1) % SETTINGS_ITEM_COUNT;
        update_display();
    }
}

static void settings_on_encoder_ccw(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_BUDDY);
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        current_settings_item = (current_settings_item - 1 + SETTINGS_ITEM_COUNT) % SETTINGS_ITEM_COUNT;
        update_display();
    }
}

static void settings_on_encoder_press(void)
{
    if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_IDLE;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_MAIN);
    }
}

static void navigate_to_subpage(void)
{
    settings_mode = SETTINGS_MODE_IDLE;
    update_display();

    switch (current_settings_item) {
        case 0: ui_switch_screen(UI_SCREEN_SETTINGS_POMODORO); break;
        case 1: ui_switch_screen(UI_SCREEN_SETTINGS_BUDDY);    break;
        case 2: ui_switch_screen(UI_SCREEN_SETTINGS_LIGHT);    break;
        case 3: ui_switch_screen(UI_SCREEN_WIFI_SAVED);        break;
        case 4: ui_switch_screen(UI_SCREEN_SETTINGS_TIME);     break;
        case 5: ui_switch_screen(UI_SCREEN_SETTINGS_SYSTEM);   break;
    }
}

static void settings_on_settings_press(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        settings_mode = SETTINGS_MODE_SELECT;
        current_settings_item = 0;
        update_display();
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        navigate_to_subpage();
    }
}

static void settings_on_encoder_long_press(void)
{
    if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_IDLE;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_MAIN);
    }
}

lv_obj_t* ui_screen_settings_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    settings_title = lv_label_create(screen);
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(settings_title, "SETTINGS");
    lv_obj_set_style_text_font(settings_title, &lv_font_montserrat_16, 0);
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 6);

    settings_list = ui_list_create(screen, 220, 180, 10, 28);

    settings_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(settings_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(settings_hint, "SET:enter");
    lv_obj_set_style_text_font(settings_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(settings_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = settings_on_encoder_cw,
        .on_encoder_ccw = settings_on_encoder_ccw,
        .on_encoder_press = settings_on_encoder_press,
        .on_encoder_long_press = settings_on_encoder_long_press,
        .on_settings_press = settings_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS, &cbs);

    update_display();

    ESP_LOGI(TAG, "Settings screen created (6 categories)");
    return screen;
}

settings_mode_t ui_screen_settings_get_mode(void)
{
    return settings_mode;
}

void ui_screen_settings_set_mode(settings_mode_t mode)
{
    settings_mode = mode;
}

int ui_screen_settings_get_current_item(void)
{
    return current_settings_item;
}
