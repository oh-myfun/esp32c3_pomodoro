#include "ui_helpers.h"
#include "ui_screen_settings_system.h"
#include "custom_font.h"
#include "i18n.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/storage_service.h"
#include "input/input_handler.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_SYSTEM";

#define SYSTEM_ITEM_COUNT 4

typedef enum { MODE_SELECT, MODE_ADJUST } sys_mode_t;

static sys_mode_t sys_mode = MODE_SELECT;
static int system_selected_item = 0;
/* system_values[0..3]: dir, lang, sleep_idx, deep_sleep_idx */
static int system_values[SYSTEM_ITEM_COUNT] = {0, 0, 1, 0};
static const int sleep_mins[] = {0, -10, -30, 1, 2, 5, 10};  /* negative = seconds; shared with deep sleep */
#define SLEEP_OPT_COUNT (sizeof(sleep_mins) / sizeof(sleep_mins[0]))
#define DEEP_SLEEP_OPT_COUNT SLEEP_OPT_COUNT

static lv_obj_t *screen = NULL;
static lv_obj_t *system_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[SYSTEM_ITEM_COUNT][20];
static char item_values[SYSTEM_ITEM_COUNT][16];
static ui_list_item_t items[SYSTEM_ITEM_COUNT];

static void update_display(void)
{
    const char *dir_opts[] = {i18n(STR_NORMAL), i18n(STR_REV)};
    const char *lang_opts[] = {i18n(STR_LANG_EN), i18n(STR_LANG_ZH)};

    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_DIRECTION));
    snprintf(item_values[0], sizeof(item_values[0]), "%s", dir_opts[system_values[0] % 2]);

    snprintf(item_keys[1], sizeof(item_keys[1]), "%s", i18n(STR_LANGUAGE));
    snprintf(item_values[1], sizeof(item_values[1]), "%s", lang_opts[system_values[1] % 2]);

    snprintf(item_keys[2], sizeof(item_keys[2]), "%s", i18n(STR_SLEEP_TIMEOUT));
    {
        int idx = system_values[2] % SLEEP_OPT_COUNT;
        int val = sleep_mins[idx];
        if (val == 0) {
            snprintf(item_values[2], sizeof(item_values[2]), "%s", i18n(STR_OFF_VAL));
        } else if (val < 0) {
            snprintf(item_values[2], sizeof(item_values[2]), i18n(STR_FMT_SEC), -val);
        } else {
            snprintf(item_values[2], sizeof(item_values[2]), i18n(STR_FMT_MIN), val);
        }
    }

    snprintf(item_keys[3], sizeof(item_keys[3]), "%s", i18n(STR_DEEP_SLEEP_TIMEOUT));
    {
        int idx = system_values[3] % DEEP_SLEEP_OPT_COUNT;
        int val = sleep_mins[idx];
        if (val == 0) {
            snprintf(item_values[3], sizeof(item_values[3]), "%s", i18n(STR_OFF_VAL));
        } else if (val < 0) {
            snprintf(item_values[3], sizeof(item_values[3]), i18n(STR_FMT_SEC), -val);
        } else {
            snprintf(item_values[3], sizeof(item_values[3]), i18n(STR_FMT_MIN), val);
        }
    }

    for (int i = 0; i < SYSTEM_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (system_list) {
        ui_list_set_selected_color(system_list,
            sys_mode == MODE_ADJUST ? UI_COLOR_ACCENT : UI_COLOR_SUCCESS);
        ui_list_set_items(system_list, items, SYSTEM_ITEM_COUNT);
        ui_list_set_selected(system_list, system_selected_item);
    }

    if (hint_label) {
        if (sys_mode == MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_TOGGLE_PRESS_BACK));
        }
    }
}

static void system_on_encoder_cw(void)
{
    if (sys_mode == MODE_ADJUST) {
        if (system_selected_item == 2) {
            system_values[2] = (system_values[2] + 1) % SLEEP_OPT_COUNT;
        } else if (system_selected_item == 3) {
            system_values[3] = (system_values[3] + 1) % DEEP_SLEEP_OPT_COUNT;
        }
        update_display();
    } else {
        system_selected_item = (system_selected_item + 1) % SYSTEM_ITEM_COUNT;
        update_display();
    }
}

static void system_on_encoder_ccw(void)
{
    if (sys_mode == MODE_ADJUST) {
        if (system_selected_item == 2) {
            system_values[2] = (system_values[2] - 1 + SLEEP_OPT_COUNT) % SLEEP_OPT_COUNT;
        } else if (system_selected_item == 3) {
            system_values[3] = (system_values[3] - 1 + DEEP_SLEEP_OPT_COUNT) % DEEP_SLEEP_OPT_COUNT;
        }
        update_display();
    } else {
        system_selected_item = (system_selected_item - 1 + SYSTEM_ITEM_COUNT) % SYSTEM_ITEM_COUNT;
        update_display();
    }
}

static void system_on_encoder_press(void)
{
    if (sys_mode == MODE_ADJUST) {
        /* Cancel: reload saved value */
        int32_t val;
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
            system_values[2] = (int)val;
        }
        int32_t dval;
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_DEEP_SLEEP_TIMEOUT, &dval) && dval >= 0 && dval < (int)DEEP_SLEEP_OPT_COUNT) {
            system_values[3] = (int)dval;
        }
        sys_mode = MODE_SELECT;
    } else {
        ui_go_back();
        return;
    }
    update_display();
}

static void system_on_settings_press(void)
{
    if (sys_mode == MODE_ADJUST) {
        /* Save and exit adjust */
        if (system_selected_item == 2) {
            extern int sleep_timeout_idx;
            sleep_timeout_idx = system_values[2];
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, system_values[2]);
        } else if (system_selected_item == 3) {
            extern int deep_sleep_timeout_idx;
            deep_sleep_timeout_idx = system_values[3];
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_DEEP_SLEEP_TIMEOUT, system_values[3]);
        }
        sys_mode = MODE_SELECT;
        update_display();
        return;
    }

    switch (system_selected_item) {
        case 0:
            system_values[0] = !system_values[0];
            input_handler_set_reverse(system_values[0]);
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, system_values[0]);
            break;
        case 1:
            system_values[1] = !system_values[1];
            i18n_set_lang(system_values[1] ? LANG_ZH : LANG_EN);
            break;
        case 2:
        case 3:
            sys_mode = MODE_ADJUST;
            break;
    }
    update_display();
}

lv_obj_t* ui_screen_settings_system_create(void)
{
    if (!screen) {
        screen = ui_create_screen();
    }
    system_list = NULL;
    hint_label = NULL;
    sys_mode = MODE_SELECT;

    
    lv_obj_t *title = ui_create_title_label(screen, i18n(STR_T_SYSTEM));


    system_list = ui_list_create(screen, 220, 196, 10, 30);

    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &val)) {
        system_values[0] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        system_values[1] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
        system_values[2] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_DEEP_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)DEEP_SLEEP_OPT_COUNT) {
        system_values[3] = (int)val;
    }

    update_display();

    
    hint_label = ui_create_hint_label(screen, i18n(STR_H_SET_TOGGLE_PRESS_BACK));


    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = system_on_encoder_cw,
        .on_encoder_ccw = system_on_encoder_ccw,
        .on_encoder_press = system_on_encoder_press,
        .on_encoder_long_press = NULL,
        .on_settings_press = system_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_SYSTEM, &cbs);

    ESP_LOGI(TAG, "Settings System screen created");
    return screen;
}
