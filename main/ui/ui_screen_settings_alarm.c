#include "ui_helpers.h"
#include "ui_screen_settings_alarm.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/storage_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_ALARM";

#define ALARM_ITEM_COUNT 1
#define KEY_TIMER_TOTAL "timer_total"
#define TIMER_MAX_MIN 360

typedef enum {
    ALARM_MODE_NAV = 0,
    ALARM_MODE_ADJUST,
} alarm_edit_mode_t;

static alarm_edit_mode_t alarm_mode = ALARM_MODE_NAV;
static int alarm_selected_item = 0;
static int alarm_duration_min = 5;

static lv_obj_t *screen = NULL;
static lv_obj_t *alarm_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[ALARM_ITEM_COUNT][20];
static char item_values[ALARM_ITEM_COUNT][20];
static ui_list_item_t items[ALARM_ITEM_COUNT];

static void update_display(void)
{
    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_ALARM_DURATION));
    {
        int h = alarm_duration_min / 60;
        int m = alarm_duration_min % 60;
        if (h > 0 && m > 0) {
            snprintf(item_values[0], sizeof(item_values[0]), i18n(STR_FMT_HOUR_MIN), h, m);
        } else if (h > 0) {
            snprintf(item_values[0], sizeof(item_values[0]), i18n(STR_FMT_HOUR), h);
        } else {
            snprintf(item_values[0], sizeof(item_values[0]), i18n(STR_FMT_MIN), m);
        }
    }
    items[0].key = item_keys[0];
    items[0].value = item_values[0];

    if (alarm_list) {
        lv_color_t color = (alarm_mode == ALARM_MODE_ADJUST)
            ? UI_COLOR_ACCENT
            : UI_COLOR_SUCCESS;
        ui_list_set_selected_color(alarm_list, color);
        ui_list_set_items(alarm_list, items, ALARM_ITEM_COUNT);
        ui_list_set_selected(alarm_list, alarm_selected_item);
    }

    if (hint_label) {
        lv_label_set_text(hint_label,
            (alarm_mode == ALARM_MODE_ADJUST)
                ? i18n(STR_H_SET_SAVE_PRESS_CANCEL)
                : i18n(STR_H_SET_EDIT_PRESS_BACK));
    }
}

static void alarm_on_encoder_cw(void)
{
    if (alarm_mode == ALARM_MODE_NAV) return;
    if (alarm_selected_item == 0) {
        int step = ui_get_encoder_step();
        if (alarm_duration_min + step <= TIMER_MAX_MIN)
            alarm_duration_min += step;
        update_display();
    }
}

static void alarm_on_encoder_ccw(void)
{
    if (alarm_mode == ALARM_MODE_NAV) return;
    if (alarm_selected_item == 0) {
        int step = ui_get_encoder_step();
        if (alarm_duration_min - step >= 1)
            alarm_duration_min -= step;
        update_display();
    }
}

static void alarm_on_encoder_press(void)
{
    if (alarm_mode == ALARM_MODE_ADJUST) {
        int32_t val = 0;
        storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_TIMER_TOTAL, &val);
        alarm_duration_min = (val >= 1 && val <= TIMER_MAX_MIN) ? (int)val : 5;
        alarm_mode = ALARM_MODE_NAV;
        update_display();
    } else {
        ui_go_back();
    }
}

static void alarm_on_settings_press(void)
{
    if (alarm_mode == ALARM_MODE_NAV) {
        alarm_mode = ALARM_MODE_ADJUST;
        update_display();
    } else {
        storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_TIMER_TOTAL, (int32_t)alarm_duration_min);
        alarm_mode = ALARM_MODE_NAV;
        update_display();
    }
}

lv_obj_t* ui_screen_settings_alarm_create(void)
{
    if (!screen) {
        screen = ui_create_screen();
    }
    alarm_list = NULL;
    hint_label = NULL;

    
    lv_obj_t *title = ui_create_title_label(screen, i18n(STR_T_ALARM));


    alarm_list = ui_list_create(screen, 220, 196, 10, 30);

    int32_t val = 0;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_TIMER_TOTAL, &val) && val >= 1 && val <= TIMER_MAX_MIN) {
        alarm_duration_min = (int)val;
    }
    alarm_mode = ALARM_MODE_NAV;
    update_display();

    
    hint_label = ui_create_hint_label(screen, i18n(STR_H_SET_EDIT_PRESS_BACK));


    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = alarm_on_encoder_cw,
        .on_encoder_ccw = alarm_on_encoder_ccw,
        .on_encoder_press = alarm_on_encoder_press,
        .on_encoder_long_press = NULL,
        .on_settings_press = alarm_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_ALARM, &cbs);

    ESP_LOGI(TAG, "Alarm settings screen created");
    return screen;
}
