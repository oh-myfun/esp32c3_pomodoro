#include "ui_screen_settings_light.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/led_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_LIGHT";

typedef enum {
    LIGHT_MODE_NAV = 0,
    LIGHT_MODE_ADJUST,
} light_edit_mode_t;

#define LIGHT_ITEM_COUNT 6
// 0: On/Off, 1: Brightness, 2: Speed, 3: Style, 4: Animation, 5: Demo

static light_edit_mode_t light_mode = LIGHT_MODE_NAV;
static int light_selected_item = 0;
static int light_values[LIGHT_ITEM_COUNT] = {1, 5, 1, 0, 2, 0};
// On=1, Bright=5, Speed=Med(1), Style=Pure(0), Anim=Scan(2), Demo_color=0(Red)

static lv_obj_t *screen = NULL;
static lv_obj_t *light_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[LIGHT_ITEM_COUNT][20];
static char item_values[LIGHT_ITEM_COUNT][12];
static ui_list_item_t items[LIGHT_ITEM_COUNT];

static void update_display(void);

static void light_on_encoder_cw(void)
{
    if (light_mode == LIGHT_MODE_NAV) {
        light_selected_item = (light_selected_item + 1) % LIGHT_ITEM_COUNT;
        update_display();
    } else if (light_mode == LIGHT_MODE_ADJUST) {
        switch (light_selected_item) {
            case 0: // On/Off
                light_values[0] = !light_values[0];
                led_service_set_enabled(light_values[0]);
                break;
            case 1: // Brightness
                if (light_values[1] < 9) {
                    light_values[1]++;
                    led_service_set_brightness(light_values[1]);
                }
                break;
            case 2: // Speed
                light_values[2] = (light_values[2] + 1) % 3;
                led_service_set_speed((led_speed_t)light_values[2]);
                break;
            case 3: // Style
                light_values[3] = (light_values[3] + 1) % 2;
                led_service_set_style((led_style_t)light_values[3]);
                break;
            case 4: // Animation
                light_values[4] = (light_values[4] + 1) % 3;
                led_service_set_animation((led_anim_t)light_values[4]);
                break;
            case 5: // Demo color
                light_values[5] = (light_values[5] + 1) % LED_DEMO_COLOR_COUNT;
                if (led_service_is_demo_active()) {
                    led_service_demo_change_color(led_demo_colors[light_values[5]]);
                }
                break;
        }
        update_display();
    }
}

static void light_on_encoder_ccw(void)
{
    if (light_mode == LIGHT_MODE_NAV) {
        light_selected_item = (light_selected_item - 1 + LIGHT_ITEM_COUNT) % LIGHT_ITEM_COUNT;
        update_display();
    } else if (light_mode == LIGHT_MODE_ADJUST) {
        switch (light_selected_item) {
            case 0: // On/Off
                light_values[0] = !light_values[0];
                led_service_set_enabled(light_values[0]);
                break;
            case 1: // Brightness
                if (light_values[1] > 1) {
                    light_values[1]--;
                    led_service_set_brightness(light_values[1]);
                }
                break;
            case 2: // Speed
                light_values[2] = (light_values[2] - 1 + 3) % 3;
                led_service_set_speed((led_speed_t)light_values[2]);
                break;
            case 3: // Style
                light_values[3] = (light_values[3] + 1) % 2;
                led_service_set_style((led_style_t)light_values[3]);
                break;
            case 4: // Animation
                light_values[4] = (light_values[4] - 1 + 3) % 3;
                led_service_set_animation((led_anim_t)light_values[4]);
                break;
            case 5: // Demo color
                light_values[5] = (light_values[5] - 1 + LED_DEMO_COLOR_COUNT) % LED_DEMO_COLOR_COUNT;
                if (led_service_is_demo_active()) {
                    led_service_demo_change_color(led_demo_colors[light_values[5]]);
                }
                break;
        }
        update_display();
    }
}

static void light_on_encoder_press(void)
{
    if (light_mode == LIGHT_MODE_ADJUST) {
        if (light_selected_item == 5 && led_service_is_demo_active()) {
            led_service_demo_stop();
        }
        light_mode = LIGHT_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

static void light_on_settings_press(void)
{
    if (light_mode == LIGHT_MODE_NAV) {
        if (light_selected_item == 5) {
            // Demo: start demo with selected color
            light_mode = LIGHT_MODE_ADJUST;
            led_service_demo_start(led_demo_colors[light_values[5]]);
            update_display();
        } else {
            light_mode = LIGHT_MODE_ADJUST;
            update_display();
        }
    } else {
        if (light_selected_item == 5 && led_service_is_demo_active()) {
            led_service_demo_stop();
        }
        light_mode = LIGHT_MODE_NAV;
        update_display();
    }
}

static void light_on_encoder_long_press(void)
{
    if (light_mode == LIGHT_MODE_ADJUST) {
        if (light_selected_item == 5 && led_service_is_demo_active()) {
            led_service_demo_stop();
        }
        light_mode = LIGHT_MODE_NAV;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

static void update_display(void)
{
    const char *speed_opts[] = {i18n(STR_SLOW), i18n(STR_MED), i18n(STR_FAST)};
    const char *style_opts[] = {i18n(STR_PURE), i18n(STR_COLOR)};
    const char *anim_opts[] = {i18n(STR_BREATH), i18n(STR_SCAN), i18n(STR_GRADIENT)};

    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_LIGHT));
    snprintf(item_values[0], sizeof(item_values[0]), "%s", light_values[0] ? i18n(STR_ON) : i18n(STR_OFF));

    snprintf(item_keys[1], sizeof(item_keys[1]), "%s", i18n(STR_BRIGHT));
    snprintf(item_values[1], sizeof(item_values[1]), "%d", light_values[1]);

    snprintf(item_keys[2], sizeof(item_keys[2]), "%s", i18n(STR_SPEED));
    snprintf(item_values[2], sizeof(item_values[2]), "%s", speed_opts[light_values[2] % 3]);

    snprintf(item_keys[3], sizeof(item_keys[3]), "%s", i18n(STR_STYLE));
    snprintf(item_values[3], sizeof(item_values[3]), "%s", style_opts[light_values[3] % 2]);

    snprintf(item_keys[4], sizeof(item_keys[4]), "%s", i18n(STR_ANIM));
    snprintf(item_values[4], sizeof(item_values[4]), "%s", anim_opts[light_values[4] % 3]);

    snprintf(item_keys[5], sizeof(item_keys[5]), "%s", i18n(STR_DEMO));
    snprintf(item_values[5], sizeof(item_values[5]), "%s", led_demo_color_names[light_values[5] % LED_DEMO_COLOR_COUNT]);

    for (int i = 0; i < LIGHT_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (light_list) {
        lv_color_t color;
        if (light_mode == LIGHT_MODE_ADJUST) {
            color = lv_color_hex(0xFFFF00);
        } else {
            color = lv_color_hex(0x00FF00);
        }
        ui_list_set_selected_color(light_list, color);
        ui_list_set_items(light_list, items, LIGHT_ITEM_COUNT);
        ui_list_set_selected(light_list, light_selected_item);
    }

    if (hint_label) {
        if (light_mode == LIGHT_MODE_ADJUST) {
            if (light_selected_item == 5 && led_service_is_demo_active()) {
                lv_label_set_text(hint_label, i18n(STR_H_SET_PRESS_STOP_DEMO));
            } else {
                lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
            }
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
        }
    }
}

lv_obj_t* ui_screen_settings_light_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    light_list = NULL;
    hint_label = NULL;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_T_LIGHT));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    light_list = ui_list_create(screen, 220, 196, 10, 30);

    // Load current settings from led_service
    light_values[0] = led_service_is_enabled() ? 1 : 0;
    light_values[1] = led_service_get_brightness();
    light_values[2] = (int)led_service_get_speed();
    light_values[3] = (int)led_service_get_style();
    light_values[4] = (int)led_service_get_animation();
    light_values[5] = 0;

    light_mode = LIGHT_MODE_NAV;
    light_selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = light_on_encoder_cw,
        .on_encoder_ccw = light_on_encoder_ccw,
        .on_encoder_press = light_on_encoder_press,
        .on_encoder_long_press = light_on_encoder_long_press,
        .on_settings_press = light_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_LIGHT, &cbs);

    ESP_LOGI(TAG, "Settings Light screen created");
    return screen;
}

void ui_screen_settings_light_refresh(void)
{
    update_display();
}
