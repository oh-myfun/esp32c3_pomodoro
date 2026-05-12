#include "ui_screen_settings_time.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/time_service.h"
#include "service/storage_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_TIME";

typedef enum {
    TIME_MODE_NAV = 0,
    TIME_MODE_ADJUST,
} time_edit_mode_t;

#define TIME_ITEM_COUNT 3

static time_edit_mode_t time_mode = TIME_MODE_NAV;
static int time_selected_item = 0;
static int time_values[TIME_ITEM_COUNT] = {8, 0, 10};

static lv_obj_t *screen = NULL;
static lv_obj_t *time_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[TIME_ITEM_COUNT][20];
static char item_values[TIME_ITEM_COUNT][20];
static ui_list_item_t items[TIME_ITEM_COUNT];

static void update_display(void)
{
    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_TIMEZONE));
    snprintf(item_values[0], sizeof(item_values[0]), "UTC%+d", time_values[0]);

    snprintf(item_keys[1], sizeof(item_keys[1]), "%s", i18n(STR_NTP_SERVER));
    snprintf(item_values[1], sizeof(item_values[1]), "%s",
             time_service_get_ntp_server_name(time_values[1]));

    snprintf(item_keys[2], sizeof(item_keys[2]), "%s", i18n(STR_NTP_INTERVAL));
    if (time_values[2] == 0) {
        snprintf(item_values[2], sizeof(item_values[2]), "%s", i18n(STR_OFF_VAL));
    } else {
        snprintf(item_values[2], sizeof(item_values[2]), i18n(STR_FMT_MIN), time_values[2]);
    }

    for (int i = 0; i < TIME_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (time_list) {
        lv_color_t color;
        if (time_mode == TIME_MODE_ADJUST) {
            color = lv_color_hex(0xFFFF00);
        } else {
            color = lv_color_hex(0x00FF00);
        }
        ui_list_set_selected_color(time_list, color);
        ui_list_set_items(time_list, items, TIME_ITEM_COUNT);
        ui_list_set_selected(time_list, time_selected_item);
    }

    if (hint_label) {
        if (time_mode == TIME_MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
        }
    }
}

static void time_on_encoder_cw(void)
{
    if (time_mode == TIME_MODE_NAV) {
        time_selected_item = (time_selected_item + 1) % TIME_ITEM_COUNT;
        update_display();
    } else {
        switch (time_selected_item) {
            case 0:
                if (time_values[0] < 14) time_values[0]++;
                break;
            case 1:
                time_values[1] = (time_values[1] + 1) % TIME_SERVICE_NTP_SERVER_COUNT;
                break;
            case 2:
                if (time_values[2] < 120) time_values[2]++;
                break;
        }
        update_display();
    }
}

static void time_on_encoder_ccw(void)
{
    if (time_mode == TIME_MODE_NAV) {
        time_selected_item = (time_selected_item - 1 + TIME_ITEM_COUNT) % TIME_ITEM_COUNT;
        update_display();
    } else {
        switch (time_selected_item) {
            case 0:
                if (time_values[0] > -12) time_values[0]--;
                break;
            case 1:
                time_values[1] = (time_values[1] - 1 + TIME_SERVICE_NTP_SERVER_COUNT) % TIME_SERVICE_NTP_SERVER_COUNT;
                break;
            case 2:
                if (time_values[2] > 0) time_values[2]--;
                break;
        }
        update_display();
    }
}

static void time_on_encoder_press(void)
{
    if (time_mode == TIME_MODE_ADJUST) {
        time_values[0] = time_service_get_timezone_offset();
        time_values[1] = time_service_get_ntp_server_index();
        time_values[2] = (int)time_service_get_sync_interval();
        time_mode = TIME_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

static void time_on_settings_press(void)
{
    if (time_mode == TIME_MODE_NAV) {
        time_mode = TIME_MODE_ADJUST;
        update_display();
    } else {
        time_service_set_timezone_offset(time_values[0]);
        time_service_set_ntp_server_index(time_values[1]);
        time_service_set_sync_interval((uint16_t)time_values[2]);
        time_mode = TIME_MODE_NAV;
        update_display();
    }
}

static void time_on_encoder_long_press(void)
{
    if (time_mode == TIME_MODE_ADJUST) {
        time_values[0] = time_service_get_timezone_offset();
        time_values[1] = time_service_get_ntp_server_index();
        time_values[2] = (int)time_service_get_sync_interval();
        time_mode = TIME_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

lv_obj_t* ui_screen_settings_time_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    time_list = NULL;
    hint_label = NULL;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_T_TIME));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    time_list = ui_list_create(screen, 220, 196, 10, 30);

    time_values[0] = time_service_get_timezone_offset();
    time_values[1] = time_service_get_ntp_server_index();
    time_values[2] = (int)time_service_get_sync_interval();

    time_mode = TIME_MODE_NAV;
    time_selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = time_on_encoder_cw,
        .on_encoder_ccw = time_on_encoder_ccw,
        .on_encoder_press = time_on_encoder_press,
        .on_encoder_long_press = time_on_encoder_long_press,
        .on_settings_press = time_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_TIME, &cbs);

    ESP_LOGI(TAG, "Settings Time screen created");
    return screen;
}

void ui_screen_settings_time_refresh(void)
{
    time_values[0] = time_service_get_timezone_offset();
    time_values[1] = time_service_get_ntp_server_index();
    time_values[2] = (int)time_service_get_sync_interval();
    update_display();
}
