#include "ui_screen_settings_sensor.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/sensor_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_SENSOR";

#define SENSOR_ITEM_COUNT 9
/* 0: interval, 1: temp_source, 2: temp_min, 3: temp_max, 4: press_min, 5: press_max, 6: alt_min, 7: alt_max, 8: reset */

typedef enum {
    SENSOR_MODE_NAV = 0,
    SENSOR_MODE_ADJUST,
} sensor_edit_mode_t;

static sensor_edit_mode_t edit_mode = SENSOR_MODE_NAV;
static int selected_item = 0;
static sensor_settings_t settings;

static lv_obj_t *screen = NULL;
static lv_obj_t *list_widget = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[SENSOR_ITEM_COUNT][20];
static char item_values[SENSOR_ITEM_COUNT][12];
static ui_list_item_t items[SENSOR_ITEM_COUNT];

/* Step values for each item (0 = non-numeric: interval, temp_source, reset) */
static const int steps[SENSOR_ITEM_COUNT] = {1, 0, 5, 5, 10, 10, 50, 50, 0};

static const str_id_t temp_src_names[TEMP_SRC_COUNT] = {
    STR_SRC_AHT20, STR_SRC_BMP280, STR_SRC_AVG
};

static void update_display(void)
{
    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_SAMPLE_INTERVAL));
    snprintf(item_values[0], sizeof(item_values[0]), "%lds", (long)settings.sample_interval);

    snprintf(item_keys[1], sizeof(item_keys[1]), "%s", i18n(STR_TEMP_SOURCE));
    snprintf(item_values[1], sizeof(item_values[1]), "%s",
             (settings.temp_source >= 0 && settings.temp_source < TEMP_SRC_COUNT)
             ? i18n(temp_src_names[settings.temp_source]) : "?");

    snprintf(item_keys[2], sizeof(item_keys[2]), "%s", i18n(STR_TEMP_MIN));
    snprintf(item_values[2], sizeof(item_values[2]), "%.1fC", settings.temp_min / 10.0f);

    snprintf(item_keys[3], sizeof(item_keys[3]), "%s", i18n(STR_TEMP_MAX));
    snprintf(item_values[3], sizeof(item_values[3]), "%.1fC", settings.temp_max / 10.0f);

    snprintf(item_keys[4], sizeof(item_keys[4]), "%s", i18n(STR_PRESS_MIN));
    snprintf(item_values[4], sizeof(item_values[4]), "%ldhPa", (long)settings.press_min);

    snprintf(item_keys[5], sizeof(item_keys[5]), "%s", i18n(STR_PRESS_MAX));
    snprintf(item_values[5], sizeof(item_values[5]), "%ldhPa", (long)settings.press_max);

    snprintf(item_keys[6], sizeof(item_keys[6]), "%s", i18n(STR_ALT_MIN));
    snprintf(item_values[6], sizeof(item_values[6]), "%ldm", (long)settings.alt_min);

    snprintf(item_keys[7], sizeof(item_keys[7]), "%s", i18n(STR_ALT_MAX));
    snprintf(item_values[7], sizeof(item_values[7]), "%ldm", (long)settings.alt_max);

    snprintf(item_keys[8], sizeof(item_keys[8]), "%s", i18n(STR_RESET));
    snprintf(item_values[8], sizeof(item_values[8]), ">>");

    for (int i = 0; i < SENSOR_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (list_widget) {
        lv_color_t color;
        if (edit_mode == SENSOR_MODE_ADJUST) {
            color = lv_color_hex(0xFFFF00);
        } else {
            color = lv_color_hex(0x00FF00);
        }
        ui_list_set_selected_color(list_widget, color);
        ui_list_set_items(list_widget, items, SENSOR_ITEM_COUNT);
        ui_list_set_selected(list_widget, selected_item);
    }

    if (hint_label) {
        if (edit_mode == SENSOR_MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SENSOR_EDIT));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
        }
    }
}

static void save_current_item(void)
{
    sensor_service_set_settings(&settings);
}

static void adjust_value(int direction)
{
    if (selected_item == 0) {
        /* Sample interval: 1-60 seconds, step 1 */
        settings.sample_interval += direction;
        if (settings.sample_interval < 1) settings.sample_interval = 1;
        if (settings.sample_interval > 60) settings.sample_interval = 60;
        update_display();
        return;
    }
    if (selected_item == 1) {
        /* Temp source: cycle through options */
        settings.temp_source = (temp_source_t)((settings.temp_source + direction + TEMP_SRC_COUNT) % TEMP_SRC_COUNT);
        update_display();
        return;
    }
    if (selected_item == 8) return; /* reset item */
    int32_t *vals[] = {NULL, NULL, &settings.temp_min, &settings.temp_max,
                       &settings.press_min, &settings.press_max,
                       &settings.alt_min, &settings.alt_max};
    *vals[selected_item] += (int32_t)steps[selected_item] * direction;
    update_display();
}

static void sensor_set_on_encoder_cw(void)
{
    if (edit_mode == SENSOR_MODE_NAV) {
        selected_item = (selected_item + 1) % SENSOR_ITEM_COUNT;
        update_display();
    } else if (edit_mode == SENSOR_MODE_ADJUST) {
        adjust_value(1);
    }
}

static void sensor_set_on_encoder_ccw(void)
{
    if (edit_mode == SENSOR_MODE_NAV) {
        selected_item = (selected_item - 1 + SENSOR_ITEM_COUNT) % SENSOR_ITEM_COUNT;
        update_display();
    } else if (edit_mode == SENSOR_MODE_ADJUST) {
        adjust_value(-1);
    }
}

static void sensor_set_on_encoder_press(void)
{
    if (edit_mode == SENSOR_MODE_ADJUST) {
        save_current_item();
        edit_mode = SENSOR_MODE_NAV;
        update_display();
    } else {
        ui_go_back();
    }
}

static void sensor_set_on_settings_press(void)
{
    if (edit_mode == SENSOR_MODE_NAV) {
        if (selected_item == 8) {
            /* Reset */
            sensor_service_reset_settings();
            sensor_service_get_settings(&settings);
            update_display();
        } else {
            edit_mode = SENSOR_MODE_ADJUST;
            update_display();
        }
    } else {
        save_current_item();
        edit_mode = SENSOR_MODE_NAV;
        update_display();
    }
}

static void sensor_set_on_encoder_long_press(void)
{
    if (edit_mode == SENSOR_MODE_ADJUST) {
        sensor_service_get_settings(&settings);
        edit_mode = SENSOR_MODE_NAV;
        update_display();
    } else {
        ui_go_back();
    }
}

lv_obj_t* ui_screen_settings_sensor_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    list_widget = NULL;
    hint_label = NULL;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_SENSOR_SETTINGS));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    list_widget = ui_list_create(screen, 220, 196, 10, 30);

    sensor_service_get_settings(&settings);
    edit_mode = SENSOR_MODE_NAV;
    selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = sensor_set_on_encoder_cw,
        .on_encoder_ccw = sensor_set_on_encoder_ccw,
        .on_encoder_press = sensor_set_on_encoder_press,
        .on_encoder_long_press = sensor_set_on_encoder_long_press,
        .on_settings_press = sensor_set_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_SENSOR, &cbs);

    ESP_LOGI(TAG, "Settings Sensor screen created");
    return screen;
}
