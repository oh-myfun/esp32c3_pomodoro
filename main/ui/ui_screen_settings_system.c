#include "ui_screen_settings_system.h"
#include "custom_font.h"
#include "i18n.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include "input/input_handler.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_SYSTEM";

#define SYSTEM_ITEM_COUNT 4

typedef enum { MODE_SELECT, MODE_ADJUST } sys_mode_t;

static sys_mode_t sys_mode = MODE_SELECT;
static int system_selected_item = 0;
static int system_values[SYSTEM_ITEM_COUNT] = {1, 0, 0, 1};  /* sound, dir, lang, sleep_idx */
static const int sleep_mins[] = {0, -10, -30, 1, 2, 5, 10};  /* negative = seconds */
#define SLEEP_OPT_COUNT (sizeof(sleep_mins) / sizeof(sleep_mins[0]))

static lv_obj_t *screen = NULL;
static lv_obj_t *system_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[SYSTEM_ITEM_COUNT][20];
static char item_values[SYSTEM_ITEM_COUNT][16];
static ui_list_item_t items[SYSTEM_ITEM_COUNT];

static void update_display(void)
{
    const char *on_off[] = {i18n(STR_OFF), i18n(STR_ON)};
    const char *dir_opts[] = {i18n(STR_NORMAL), i18n(STR_REV)};
    const char *lang_opts[] = {i18n(STR_LANG_EN), i18n(STR_LANG_ZH)};

    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_SOUND));
    snprintf(item_values[0], sizeof(item_values[0]), "%s", on_off[system_values[0] % 2]);

    snprintf(item_keys[1], sizeof(item_keys[1]), "%s", i18n(STR_DIRECTION));
    snprintf(item_values[1], sizeof(item_values[1]), "%s", dir_opts[system_values[1] % 2]);

    snprintf(item_keys[2], sizeof(item_keys[2]), "%s", i18n(STR_LANGUAGE));
    snprintf(item_values[2], sizeof(item_values[2]), "%s", lang_opts[system_values[2] % 2]);

    snprintf(item_keys[3], sizeof(item_keys[3]), "%s", i18n(STR_SLEEP_TIMEOUT));
    {
        int idx = system_values[3] % SLEEP_OPT_COUNT;
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
            sys_mode == MODE_ADJUST ? lv_color_hex(0xFFAA00) : lv_color_hex(0x00FF00));
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
        if (system_selected_item == 3) {
            system_values[3] = (system_values[3] + 1) % SLEEP_OPT_COUNT;
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
        if (system_selected_item == 3) {
            system_values[3] = (system_values[3] - 1 + SLEEP_OPT_COUNT) % SLEEP_OPT_COUNT;
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
            system_values[3] = (int)val;
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
        if (system_selected_item == 3) {
            extern int sleep_timeout_idx;
            sleep_timeout_idx = system_values[3];
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, system_values[3]);
        }
        sys_mode = MODE_SELECT;
        update_display();
        return;
    }

    switch (system_selected_item) {
        case 0:
            system_values[0] = !system_values[0];
            sound_service_set_enabled(system_values[0]);
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, system_values[0]);
            break;
        case 1:
            system_values[1] = !system_values[1];
            input_handler_set_reverse(system_values[1]);
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, system_values[1]);
            break;
        case 2:
            system_values[2] = !system_values[2];
            i18n_set_lang(system_values[2] ? LANG_ZH : LANG_EN);
            break;
        case 3:
            sys_mode = MODE_ADJUST;
            break;
    }
    update_display();
}

static void system_on_encoder_long_press(void)
{
    if (sys_mode == MODE_ADJUST) {
        int32_t val;
        if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
            system_values[3] = (int)val;
        }
        sys_mode = MODE_SELECT;
        update_display();
    } else {
        ui_go_back();
    }
}

lv_obj_t* ui_screen_settings_system_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    system_list = NULL;
    hint_label = NULL;
    sys_mode = MODE_SELECT;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_T_SYSTEM));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    system_list = ui_list_create(screen, 220, 196, 10, 30);

    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, &val)) {
        system_values[0] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &val)) {
        system_values[1] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        system_values[2] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
        system_values[3] = (int)val;
    }

    system_selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_TOGGLE_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = system_on_encoder_cw,
        .on_encoder_ccw = system_on_encoder_ccw,
        .on_encoder_press = system_on_encoder_press,
        .on_encoder_long_press = system_on_encoder_long_press,
        .on_settings_press = system_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_SYSTEM, &cbs);

    ESP_LOGI(TAG, "Settings System screen created");
    return screen;
}

void ui_screen_settings_system_refresh(void)
{
    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SOUND, &val)) {
        system_values[0] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &val)) {
        system_values[1] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        system_values[2] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_SLEEP_TIMEOUT, &val) && val >= 0 && val < (int)SLEEP_OPT_COUNT) {
        system_values[3] = (int)val;
    }
    sys_mode = MODE_SELECT;
    update_display();
}
