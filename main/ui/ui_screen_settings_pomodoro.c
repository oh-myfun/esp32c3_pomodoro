#include "ui_screen_settings_pomodoro.h"
#include "ui_manager.h"
#include "ui_screen_settings.h"
#include "ui_list.h"
#include "pomodoro/pomodoro_engine.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_POMODORO";

static void pomo_set_on_encoder_cw(void)
{
    ui_screen_settings_adjust_up();
}

static void pomo_set_on_encoder_ccw(void)
{
    ui_screen_settings_adjust_down();
}

static void pomo_set_on_settings_press(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

static lv_obj_t *screen = NULL;
static lv_obj_t *pomodoro_list = NULL;

static char item_keys[4][20];
static char item_values[4][10];
static ui_list_item_t items[4];

static void update_display(void)
{
    pomodoro_settings_t settings = pomodoro_engine_get_settings();
    
    snprintf(item_keys[0], sizeof(item_keys[0]), "Work");
    snprintf(item_values[0], sizeof(item_values[0]), "%d min", settings.work_minutes);
    
    snprintf(item_keys[1], sizeof(item_keys[1]), "Break");
    snprintf(item_values[1], sizeof(item_values[1]), "%d min", settings.break_minutes);
    
    snprintf(item_keys[2], sizeof(item_keys[2]), "Long Break");
    snprintf(item_values[2], sizeof(item_values[2]), "%d min", settings.long_break_minutes);
    
    snprintf(item_keys[3], sizeof(item_keys[3]), "Cycles");
    snprintf(item_values[3], sizeof(item_values[3]), "%d", settings.cycles_until_long_break);
    
    for (int i = 0; i < 4; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }
    
    if (pomodoro_list) {
        ui_list_set_items(pomodoro_list, items, 4);
    }
}

lv_obj_t* ui_screen_settings_pomodoro_create(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Pomodoro");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    pomodoro_list = ui_list_create(screen, 220, 180, 10, 30);
    
    update_display();

    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint, "Rotate: nav | SET: back");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = pomo_set_on_encoder_cw,
        .on_encoder_ccw = pomo_set_on_encoder_ccw,
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
