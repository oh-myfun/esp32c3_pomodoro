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

#define SYSTEM_ITEM_COUNT 2

static int system_selected_item = 0;
/* system_values[0..1]: dir, lang */
static int system_values[SYSTEM_ITEM_COUNT] = {0, 0};

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

    for (int i = 0; i < SYSTEM_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (system_list) {
        ui_list_set_selected_color(system_list, UI_COLOR_SUCCESS);
        ui_list_set_items(system_list, items, SYSTEM_ITEM_COUNT);
        ui_list_set_selected(system_list, system_selected_item);
    }

    if (hint_label) {
        lv_label_set_text(hint_label, i18n(STR_H_SET_TOGGLE_PRESS_BACK));
    }
}

static void system_on_encoder_cw(void)
{
    system_selected_item = (system_selected_item + 1) % SYSTEM_ITEM_COUNT;
    update_display();
}

static void system_on_encoder_ccw(void)
{
    system_selected_item = (system_selected_item - 1 + SYSTEM_ITEM_COUNT) % SYSTEM_ITEM_COUNT;
    update_display();
}

static void system_on_encoder_press(void)
{
    ui_go_back();
}

static void system_on_settings_press(void)
{
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


    lv_obj_t *title = ui_create_title_label(screen, i18n(STR_T_SYSTEM));


    system_list = ui_list_create(screen, 220, 196, 10, 30);

    int32_t val;
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &val)) {
        system_values[0] = (int)val;
    }
    if (storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_LANG, &val)) {
        system_values[1] = (int)val;
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
