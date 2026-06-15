#include "ui_helpers.h"
#include "ui_screen_settings_light_demo.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/led_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_LIGHT_DEMO";

#define LIGHT_DEMO_COUNT LED_DEMO_COLOR_COUNT

static lv_obj_t *screen = NULL;
static lv_obj_t *demo_list = NULL;
static lv_obj_t *hint_label = NULL;

static int selected = 0;
static int demoed_idx = -1;   /* index currently being demoed, -1 if none */

static char item_keys[LIGHT_DEMO_COUNT][20];
static char item_values[LIGHT_DEMO_COUNT][4];
static ui_list_item_t items[LIGHT_DEMO_COUNT];

static const str_id_t demo_name_ids[LIGHT_DEMO_COUNT] = {
    STR_DEMO_WORK, STR_DEMO_BREAK, STR_DEMO_LONG_BREAK, STR_DEMO_PAUSED, STR_DEMO_SAD
};

static void update_display(void)
{
    for (int i = 0; i < LIGHT_DEMO_COUNT; i++) {
        snprintf(item_keys[i], sizeof(item_keys[i]), "%s", i18n(demo_name_ids[i]));
        snprintf(item_values[i], sizeof(item_values[i]), "%s", (i == demoed_idx) ? "\xe2\x96\xb6" : " ");
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (demo_list) {
        ui_list_set_items(demo_list, items, LIGHT_DEMO_COUNT);
        ui_list_set_selected(demo_list, selected);
    }

    if (hint_label) {
        lv_label_set_text(hint_label, i18n(STR_H_SET_SELECT_PRESS_BACK));
    }
}

static void demo_on_encoder_cw(void)
{
    selected = (selected + 1) % LIGHT_DEMO_COUNT;
    update_display();
}

static void demo_on_encoder_ccw(void)
{
    selected = (selected - 1 + LIGHT_DEMO_COUNT) % LIGHT_DEMO_COUNT;
    update_display();
}

static void demo_on_encoder_press(void)
{
    if (demoed_idx >= 0) {
        led_service_demo_stop();
        demoed_idx = -1;
    }
    ui_go_back();
}

static void demo_on_settings_press(void)
{
    if (demoed_idx < 0) {
        led_service_demo_start(led_demo_colors[selected]);
        demoed_idx = selected;
    } else if (demoed_idx == selected) {
        led_service_demo_stop();
        demoed_idx = -1;
    } else {
        led_service_demo_change_color(led_demo_colors[selected]);
        demoed_idx = selected;
    }
    update_display();
}

lv_obj_t *ui_screen_settings_light_demo_create(void)
{
    if (!screen) {
        screen = ui_create_screen();
    }
    demo_list = NULL;
    hint_label = NULL;
    demoed_idx = -1;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_DEMO));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    demo_list = ui_list_create(screen, 220, 196, 10, 30);

    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, UI_COLOR_TEXT_HINT, 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_SELECT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, UI_HINT_BOTTOM_OFFSET);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = demo_on_encoder_cw,
        .on_encoder_ccw = demo_on_encoder_ccw,
        .on_encoder_press = demo_on_encoder_press,
        .on_encoder_long_press = NULL,
        .on_settings_press = demo_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_LIGHT_DEMO, &cbs);

    ESP_LOGI(TAG, "Light demo screen created");
    return screen;
}
