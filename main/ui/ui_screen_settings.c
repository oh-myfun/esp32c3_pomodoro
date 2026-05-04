#include "ui_screen_settings.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/wifi_service.h"
#include "service/time_service.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include "service/led_service.h"
#include "input/input_handler.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS";

#define SETTINGS_ITEM_COUNT 13

static lv_obj_t *settings_title = NULL;
static lv_obj_t *settings_list = NULL;
static lv_obj_t *settings_hint = NULL;

static settings_mode_t settings_mode = SETTINGS_MODE_IDLE;
static int settings_values[SETTINGS_ITEM_COUNT] = {
    0, 8, 25, 0, 0, 1, 10,  // 0-6: existing
    1, 5, 1, 0, 2, 0         // 7-12: LED on=1, bright=5, speed=Medium(1), style=Pure(0), anim=Scan(2), demo_color=0(Red)
};
static int current_settings_item = 0;

static char item_keys[SETTINGS_ITEM_COUNT][20];
static char item_values[SETTINGS_ITEM_COUNT][12];
static ui_list_item_t items[SETTINGS_ITEM_COUNT];

static void update_display(void);

/* ---- Callbacks ---- */

static void settings_on_encoder_cw(void)
{
    settings_mode_t mode = ui_screen_settings_get_mode();
    if (mode == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_MAIN);
    } else if (mode == SETTINGS_MODE_SELECT) {
        ui_screen_settings_select_next();
    } else if (mode == SETTINGS_MODE_ADJUST) {
        ui_screen_settings_adjust_up();
    }
}

static void settings_on_encoder_ccw(void)
{
    settings_mode_t mode = ui_screen_settings_get_mode();
    if (mode == SETTINGS_MODE_IDLE) {
        ui_switch_screen(UI_SCREEN_BUDDY);
    } else if (mode == SETTINGS_MODE_SELECT) {
        ui_screen_settings_select_prev();
    } else if (mode == SETTINGS_MODE_ADJUST) {
        ui_screen_settings_adjust_down();
    }
}

static void settings_on_encoder_press(void)
{
    settings_mode_t mode = ui_screen_settings_get_mode();
    if (mode == SETTINGS_MODE_ADJUST) {
        if (current_settings_item == 12 && led_service_is_demo_active()) {
            led_service_demo_stop();
        }
        ui_screen_settings_exit();
    } else if (mode == SETTINGS_MODE_SELECT) {
        ui_switch_screen(UI_SCREEN_MAIN);
    }
}

static void settings_on_settings_press(void)
{
    settings_mode_t mode = ui_screen_settings_get_mode();
    if (mode == SETTINGS_MODE_IDLE) {
        ui_screen_settings_enter();
    } else if (mode == SETTINGS_MODE_SELECT) {
        int item = ui_screen_settings_get_current_item();
        if (item == 3) {  // WiFi
            settings_mode = SETTINGS_MODE_IDLE;
            update_display();
            ui_switch_screen(UI_SCREEN_WIFI_SAVED);
        } else if (item == 2) {  // Pomodoro
            settings_mode = SETTINGS_MODE_IDLE;
            update_display();
            ui_switch_screen(UI_SCREEN_SETTINGS_POMODORO);
        } else if (item == 12) {  // LED Demo
            settings_mode = SETTINGS_MODE_ADJUST;
            led_service_demo_start(led_demo_colors[settings_values[12] % LED_DEMO_COLOR_COUNT]);
            update_display();
        } else {
            ui_screen_settings_enter_adjust();
        }
    } else if (mode == SETTINGS_MODE_ADJUST) {
        if (current_settings_item == 12 && led_service_is_demo_active()) {
            led_service_demo_stop();
        }
        ui_screen_settings_exit();
    }
}

/* ---- Display ---- */

static const char *settings_names[SETTINGS_ITEM_COUNT] = {
    "Language", "Timezone", "Pomodoro", "WiFi", "Direction", "Sound", "NTP Interval",
    "LED", "LED Bright", "LED Speed", "LED Style", "LED Anim", "LED Demo"
};

static void update_display(void)
{
    static const char *lang_opts[] = {"English", "Chinese"};

    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        strncpy(item_keys[i], settings_names[i], sizeof(item_keys[i]) - 1);

        switch (i) {
            case 0:  // Language
                snprintf(item_values[i], sizeof(item_values[i]), "%s", lang_opts[settings_values[i] % 2]);
                break;
            case 1:  // Timezone
                snprintf(item_values[i], sizeof(item_values[i]), "UTC%+d", settings_values[i]);
                break;
            case 2:  // Pomodoro (sub-screen)
            case 3:  // WiFi (sub-screen)
                snprintf(item_values[i], sizeof(item_values[i]), ">");
                break;
            case 4:  // Direction
                snprintf(item_values[i], sizeof(item_values[i]), "%s", settings_values[i] ? "Rev" : "Normal");
                break;
            case 5:  // Sound
                snprintf(item_values[i], sizeof(item_values[i]), "%s", settings_values[i] ? "On" : "Off");
                break;
            case 6:  // NTP Interval
                snprintf(item_values[i], sizeof(item_values[i]), "%d min", settings_values[i]);
                break;
            case 7:  // LED on/off
                snprintf(item_values[i], sizeof(item_values[i]), "%s", settings_values[i] ? "On" : "Off");
                break;
            case 8:  // LED Brightness
                snprintf(item_values[i], sizeof(item_values[i]), "%d", settings_values[i]);
                break;
            case 9:  // LED Speed
                snprintf(item_values[i], sizeof(item_values[i]), "%s",
                         (const char*[]){"Slow", "Med", "Fast"}[settings_values[i] % 3]);
                break;
            case 10: // LED Style
                snprintf(item_values[i], sizeof(item_values[i]), "%s",
                         (const char*[]){"Pure", "Color"}[settings_values[i] % 2]);
                break;
            case 11: // LED Anim
                snprintf(item_values[i], sizeof(item_values[i]), "%s",
                         (const char*[]){"Breath", "Scan", "Gradient"}[settings_values[i] % 3]);
                break;
            case 12: // LED Demo
                snprintf(item_values[i], sizeof(item_values[i]), "%s", led_demo_color_names[settings_values[i] % LED_DEMO_COLOR_COUNT]);
                break;
            default:
                snprintf(item_values[i], sizeof(item_values[i]), "%d", settings_values[i]);
                break;
        }
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (settings_list) {
        lv_color_t color;
        if (settings_mode == SETTINGS_MODE_ADJUST) {
            color = lv_color_hex(0xFFFF00);
        } else if (settings_mode == SETTINGS_MODE_SELECT) {
            color = lv_color_hex(0x00FF00);
        } else {
            color = lv_color_hex(0xFFFFFF);
        }
        ui_list_set_selected_color(settings_list, color);
        ui_list_set_items(settings_list, items, SETTINGS_ITEM_COUNT);
        ui_list_set_selected(settings_list, current_settings_item);
    }

    if (settings_hint) {
        const char *text;
        if (settings_mode == SETTINGS_MODE_ADJUST) {
            text = "SET/Press:back";
        } else if (settings_mode == SETTINGS_MODE_SELECT) {
            text = "SET:select|Press:back";
        } else {
            text = "SET:enter";
        }
        lv_label_set_text(settings_hint, text);
    }
}

/* ---- Public API ---- */

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
        .on_settings_press = settings_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS, &cbs);

    /* Load stored values */
    settings_values[1] = time_service_get_timezone_offset();
    int32_t stored_rev = 0;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "enc_rev", &stored_rev);
    settings_values[4] = stored_rev;
    input_handler_set_reverse(stored_rev != 0);

    int32_t sound_val = 1;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, "sound_on", &sound_val);
    settings_values[5] = sound_val;

    settings_values[6] = time_service_get_sync_interval();

    settings_values[7] = led_service_is_enabled() ? 1 : 0;
    settings_values[8] = led_service_get_brightness();
    settings_values[9] = (int)led_service_get_speed();
    settings_values[10] = (int)led_service_get_style();
    settings_values[11] = (int)led_service_get_animation();
    settings_values[12] = 0;

    update_display();

    ESP_LOGI(TAG, "Settings screen created");
    return screen;
}

void ui_screen_settings_update(int *values, int selected, settings_mode_t mode)
{
    update_display();
}

void ui_screen_settings_set_hint(const char *hint)
{
    if (settings_hint == NULL) return;
    lv_label_set_text(settings_hint, hint);
}

void ui_screen_settings_enter(void)
{
    if (settings_mode == SETTINGS_MODE_IDLE) {
        settings_mode = SETTINGS_MODE_SELECT;
        current_settings_item = 0;
        update_display();
    }
}

void ui_screen_settings_exit(void)
{
    if (settings_mode == SETTINGS_MODE_ADJUST) {
        settings_mode = SETTINGS_MODE_SELECT;
    } else if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_IDLE;
    }
    update_display();
}

settings_mode_t ui_screen_settings_get_mode(void)
{
    return settings_mode;
}

void ui_screen_settings_set_mode(settings_mode_t mode)
{
    settings_mode = mode;
}

void ui_screen_settings_select_next(void)
{
    if (settings_mode != SETTINGS_MODE_SELECT) return;
    current_settings_item = (current_settings_item + 1) % SETTINGS_ITEM_COUNT;
    update_display();
}

void ui_screen_settings_select_prev(void)
{
    if (settings_mode != SETTINGS_MODE_SELECT) return;
    current_settings_item = (current_settings_item - 1 + SETTINGS_ITEM_COUNT) % SETTINGS_ITEM_COUNT;
    update_display();
}

void ui_screen_settings_enter_adjust(void)
{
    if (settings_mode == SETTINGS_MODE_SELECT) {
        settings_mode = SETTINGS_MODE_ADJUST;
        update_display();
    }
}

void ui_screen_settings_adjust_up(void)
{
    if (settings_mode != SETTINGS_MODE_ADJUST) return;

    switch (current_settings_item) {
        case 0:  // Language
            settings_values[current_settings_item] = (settings_values[current_settings_item] + 1) % 2;
            break;
        case 1:  // Timezone
            if (settings_values[current_settings_item] < 14) {
                settings_values[current_settings_item]++;
                time_service_set_timezone_offset(settings_values[current_settings_item]);
            }
            break;
        case 4:  // Direction
            settings_values[current_settings_item] = !settings_values[current_settings_item];
            input_handler_set_reverse(settings_values[current_settings_item]);
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, "enc_rev", settings_values[current_settings_item]);
            break;
        case 5:  // Sound
            settings_values[current_settings_item] = !settings_values[current_settings_item];
            sound_service_set_enabled(settings_values[current_settings_item]);
            break;
        case 6:  // NTP Interval
            if (settings_values[current_settings_item] < 60) {
                settings_values[current_settings_item]++;
                time_service_set_sync_interval(settings_values[current_settings_item]);
            }
            break;
        case 7:  // LED on/off
            settings_values[current_settings_item] = !settings_values[current_settings_item];
            led_service_set_enabled(settings_values[current_settings_item]);
            break;
        case 8:  // LED Brightness
            if (settings_values[current_settings_item] < 10) {
                settings_values[current_settings_item]++;
                led_service_set_brightness(settings_values[current_settings_item]);
            }
            break;
        case 9:  // LED Speed
            settings_values[current_settings_item] = (settings_values[current_settings_item] + 1) % 3;
            led_service_set_speed((led_speed_t)settings_values[current_settings_item]);
            break;
        case 10: // LED Style
            settings_values[current_settings_item] = (settings_values[current_settings_item] + 1) % 2;
            led_service_set_style((led_style_t)settings_values[current_settings_item]);
            break;
        case 11: // LED Anim
            settings_values[current_settings_item] = (settings_values[current_settings_item] + 1) % 3;
            led_service_set_animation((led_anim_t)settings_values[current_settings_item]);
            break;
        case 12: // LED Demo color change
            settings_values[current_settings_item] = (settings_values[current_settings_item] + 1) % LED_DEMO_COLOR_COUNT;
            if (led_service_is_demo_active()) {
                led_service_demo_change_color(led_demo_colors[settings_values[current_settings_item]]);
            }
            break;
    }
    update_display();
}

void ui_screen_settings_adjust_down(void)
{
    if (settings_mode != SETTINGS_MODE_ADJUST) return;

    switch (current_settings_item) {
        case 0:  // Language
            settings_values[current_settings_item] = (settings_values[current_settings_item] - 1 + 2) % 2;
            break;
        case 1:  // Timezone
            if (settings_values[current_settings_item] > -12) {
                settings_values[current_settings_item]--;
                time_service_set_timezone_offset(settings_values[current_settings_item]);
            }
            break;
        case 4:  // Direction
            settings_values[current_settings_item] = !settings_values[current_settings_item];
            input_handler_set_reverse(settings_values[current_settings_item]);
            storage_save_int(STORAGE_NAMESPACE_SETTINGS, "enc_rev", settings_values[current_settings_item]);
            break;
        case 5:  // Sound
            settings_values[current_settings_item] = !settings_values[current_settings_item];
            sound_service_set_enabled(settings_values[current_settings_item]);
            break;
        case 6:  // NTP Interval
            if (settings_values[current_settings_item] > 1) {
                settings_values[current_settings_item]--;
                time_service_set_sync_interval(settings_values[current_settings_item]);
            }
            break;
        case 7:
            settings_values[current_settings_item] = !settings_values[current_settings_item];
            led_service_set_enabled(settings_values[current_settings_item]);
            break;
        case 8:
            if (settings_values[current_settings_item] > 1) {
                settings_values[current_settings_item]--;
                led_service_set_brightness(settings_values[current_settings_item]);
            }
            break;
        case 9:
            settings_values[current_settings_item] = (settings_values[current_settings_item] - 1 + 3) % 3;
            led_service_set_speed((led_speed_t)settings_values[current_settings_item]);
            break;
        case 10:
            settings_values[current_settings_item] = (settings_values[current_settings_item] + 1) % 2;
            led_service_set_style((led_style_t)settings_values[current_settings_item]);
            break;
        case 11:
            settings_values[current_settings_item] = (settings_values[current_settings_item] - 1 + 3) % 3;
            led_service_set_animation((led_anim_t)settings_values[current_settings_item]);
            break;
        case 12:
            settings_values[current_settings_item] = (settings_values[current_settings_item] - 1 + LED_DEMO_COLOR_COUNT) % LED_DEMO_COLOR_COUNT;
            if (led_service_is_demo_active()) {
                led_service_demo_change_color(led_demo_colors[settings_values[current_settings_item]]);
            }
            break;
    }
    update_display();
}

int ui_screen_settings_get_current_item(void)
{
    return current_settings_item;
}

int* ui_screen_settings_get_values(void)
{
    return settings_values;
}
