#include "ui_screen_settings_pomodoro.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "pomodoro/pomodoro_engine.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_POMODORO";

typedef enum {
    POMO_MODE_NAV = 0,      /* Encoder rotates to navigate items */
    POMO_MODE_ADJUST,       /* Encoder rotates to adjust value */
} pomo_edit_mode_t;

static pomo_edit_mode_t pomo_mode = POMO_MODE_NAV;
static int pomo_selected_item = 0;
static const int POMO_ITEM_COUNT = 7;
static bool reset_confirmed = false;
static bool default_confirmed = false;

static void update_display(void);

static void pomo_set_on_encoder_cw(void)
{
    if (pomo_mode == POMO_MODE_NAV) {
        if (default_confirmed || reset_confirmed) return;
        pomo_selected_item = (pomo_selected_item + 1) % POMO_ITEM_COUNT;
        update_display();
    } else if (pomo_mode == POMO_MODE_ADJUST) {
        pomodoro_settings_t settings = pomodoro_engine_get_settings();
        switch (pomo_selected_item) {
            case 0: /* Work minutes */
                if (settings.work_minutes < 120) {
                    pomodoro_engine_set_work_minutes(settings.work_minutes + 1);
                }
                break;
            case 1: /* Break minutes */
                if (settings.break_minutes < 60) {
                    pomodoro_engine_set_break_minutes(settings.break_minutes + 1);
                }
                break;
            case 2: /* Long break minutes */
                if (settings.long_break_minutes < 60) {
                    pomodoro_engine_set_long_break_minutes(settings.long_break_minutes + 1);
                }
                break;
            case 3: /* Cycles until long break */
                if (settings.cycles_until_long_break < 10) {
                    pomodoro_engine_set_cycles(settings.cycles_until_long_break + 1);
                }
                break;
            case 4: /* Mode */
                pomodoro_engine_set_manual_mode(!pomodoro_engine_get_manual_mode());
                break;
        }
        update_display();
    }
}

static void pomo_set_on_encoder_ccw(void)
{
    if (pomo_mode == POMO_MODE_NAV) {
        if (default_confirmed || reset_confirmed) return;
        pomo_selected_item = (pomo_selected_item - 1 + POMO_ITEM_COUNT) % POMO_ITEM_COUNT;
        update_display();
    } else if (pomo_mode == POMO_MODE_ADJUST) {
        pomodoro_settings_t settings = pomodoro_engine_get_settings();
        switch (pomo_selected_item) {
            case 0: /* Work minutes */
                if (settings.work_minutes > 1) {
                    pomodoro_engine_set_work_minutes(settings.work_minutes - 1);
                }
                break;
            case 1: /* Break minutes */
                if (settings.break_minutes > 1) {
                    pomodoro_engine_set_break_minutes(settings.break_minutes - 1);
                }
                break;
            case 2: /* Long break minutes */
                if (settings.long_break_minutes > 1) {
                    pomodoro_engine_set_long_break_minutes(settings.long_break_minutes - 1);
                }
                break;
            case 3: /* Cycles until long break */
                if (settings.cycles_until_long_break > 1) {
                    pomodoro_engine_set_cycles(settings.cycles_until_long_break - 1);
                }
                break;
            case 4: /* Mode */
                pomodoro_engine_set_manual_mode(!pomodoro_engine_get_manual_mode());
                break;
        }
        update_display();
    }
}

static void pomo_set_on_encoder_press(void)
{
    if (pomo_mode == POMO_MODE_ADJUST) {
        pomo_mode = POMO_MODE_NAV;
        pomodoro_engine_load_state();
        update_display();
    } else if (default_confirmed || reset_confirmed) {
        default_confirmed = false;
        reset_confirmed = false;
        update_display();
    } else {
        ui_go_back();
    }
}

static void pomo_set_on_settings_press(void)
{
    if (pomo_mode == POMO_MODE_NAV) {
        if (pomo_selected_item == 4) {  // Mode
            pomodoro_engine_set_manual_mode(!pomodoro_engine_get_manual_mode());
            update_display();
        } else if (pomo_selected_item == 5) {  // Default
            if (default_confirmed) {
                pomodoro_engine_set_work_minutes(25);
                pomodoro_engine_set_break_minutes(5);
                pomodoro_engine_set_long_break_minutes(15);
                pomodoro_engine_set_cycles(4);
                default_confirmed = false;
                ESP_LOGI(TAG, "Pomodoro settings restored to defaults");
            } else {
                default_confirmed = true;
            }
            update_display();
        } else if (pomo_selected_item == 6) {  // Reset
            if (reset_confirmed) {
                pomodoro_engine_reset();
                reset_confirmed = false;
                ESP_LOGI(TAG, "Pomodoro statistics reset");
            } else {
                reset_confirmed = true;
            }
            update_display();
        } else {
            pomo_mode = POMO_MODE_ADJUST;
            update_display();
        }
    } else {
        pomo_mode = POMO_MODE_NAV;
        pomodoro_engine_save_state();
        update_display();
    }
}

static void pomo_set_on_encoder_long_press(void)
{
    /* Cancel adjust or go back */
    if (pomo_mode == POMO_MODE_ADJUST) {
        pomo_mode = POMO_MODE_NAV;
        /* Reload settings to discard changes */
        pomodoro_engine_load_state();
        ESP_LOGI(TAG, "Cancelled pomodoro settings changes");
        update_display();
    } else {
        ui_go_back();
    }
}

static lv_obj_t *screen = NULL;
static lv_obj_t *pomodoro_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[7][20];
static char item_values[7][12];
static ui_list_item_t items[7];

static void update_display(void)
{
    pomodoro_settings_t settings = pomodoro_engine_get_settings();
    pomodoro_state_t state = pomodoro_engine_get_state();

    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_WORK));
    snprintf(item_values[0], sizeof(item_values[0]), i18n(STR_FMT_MIN), settings.work_minutes);

    snprintf(item_keys[1], sizeof(item_keys[1]), "%s", i18n(STR_BREAK));
    snprintf(item_values[1], sizeof(item_values[1]), i18n(STR_FMT_MIN), settings.break_minutes);

    snprintf(item_keys[2], sizeof(item_keys[2]), "%s", i18n(STR_LONG_BREAK));
    snprintf(item_values[2], sizeof(item_values[2]), i18n(STR_FMT_MIN), settings.long_break_minutes);

    snprintf(item_keys[3], sizeof(item_keys[3]), "%s", i18n(STR_CYCLES));
    snprintf(item_values[3], sizeof(item_values[3]), "%d", settings.cycles_until_long_break);

    snprintf(item_keys[4], sizeof(item_keys[4]), "%s", i18n(STR_MODE));
    snprintf(item_values[4], sizeof(item_values[4]), "%s", pomodoro_engine_get_manual_mode() ? i18n(STR_MANUAL) : i18n(STR_AUTO));

    snprintf(item_keys[5], sizeof(item_keys[5]), "%s", i18n(STR_DEFAULT));
    snprintf(item_values[5], sizeof(item_values[5]), "⇨");

    snprintf(item_keys[6], sizeof(item_keys[6]), "%s", i18n(STR_RESET));
    snprintf(item_values[6], sizeof(item_values[6]), i18n(STR_FMT_DONE), state.completed_count);

    for (int i = 0; i < 7; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (pomodoro_list) {
        if (pomo_mode == POMO_MODE_ADJUST || default_confirmed || reset_confirmed) {
            ui_list_set_selected_color(pomodoro_list, lv_color_hex(0xFFFF00));
        } else {
            ui_list_set_selected_color(pomodoro_list, lv_color_hex(0x00FF00));
        }
        ui_list_set_items(pomodoro_list, items, 7);
        ui_list_set_selected(pomodoro_list, pomo_selected_item);
    }

    if (hint_label) {
        if (pomo_selected_item == 5 && default_confirmed) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_CONFIRM_DEFAULT));
        } else if (pomo_selected_item == 6 && reset_confirmed) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_CONFIRM_RESET));
        } else if (pomo_mode == POMO_MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
        }
    }

    /* Confirmation is per-interaction, clear on nav away */
    if (pomo_selected_item != 5) {
        default_confirmed = false;
    }
    if (pomo_selected_item != 6) {
        reset_confirmed = false;
    }
}

lv_obj_t* ui_screen_settings_pomodoro_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    pomodoro_list = NULL;
    hint_label = NULL;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_T_POMODORO));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    pomodoro_list = ui_list_create(screen, 220, 196, 10, 30);

    pomo_mode = POMO_MODE_NAV;
    pomo_selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = pomo_set_on_encoder_cw,
        .on_encoder_ccw = pomo_set_on_encoder_ccw,
        .on_encoder_press = pomo_set_on_encoder_press,
        .on_encoder_long_press = pomo_set_on_encoder_long_press,
        .on_settings_press = pomo_set_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_POMODORO, &cbs);

    ESP_LOGI(TAG, "Settings Pomodoro screen created");
    return screen;
}

void ui_screen_settings_pomodoro_refresh(void)
{
    update_display();
}
