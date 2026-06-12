#pragma once

#include "lvgl.h"

typedef enum {
    UI_SCREEN_MAIN = 0,
    UI_SCREEN_POMODORO,
    UI_SCREEN_BUDDY,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_SETTINGS_POMODORO,
    UI_SCREEN_SETTINGS_ALARM,
    UI_SCREEN_WIFI_LIST,
    UI_SCREEN_TEXT_INPUT,
    UI_SCREEN_WIFI_SAVED,
    UI_SCREEN_SETTINGS_LIGHT,
    UI_SCREEN_SETTINGS_BUDDY,
    UI_SCREEN_SETTINGS_TIME,
    UI_SCREEN_SETTINGS_SYSTEM,
    UI_SCREEN_SETTINGS_SOUND,
    UI_SCREEN_SETTINGS_LIGHT_DEMO,
    UI_SCREEN_SETTINGS_SOUND_DEMO,
    UI_SCREEN_SETTINGS_DEBUG,
    UI_SCREEN_BRIDGE_SCAN,
    UI_SCREEN_SENSOR,
    UI_SCREEN_SETTINGS_SENSOR,
    UI_SCREEN_PRESSURE_INFO,
    UI_SCREEN_COUNT
} ui_screen_id_t;

typedef struct {
    void (*on_encoder_cw)(void);
    void (*on_encoder_ccw)(void);
    void (*on_encoder_press)(void);
    void (*on_encoder_long_press)(void);
    void (*on_settings_press)(void);
    void (*on_settings_long_press)(void);
    const char *(*on_long_press_hint)(bool top_key);  /* returns action text or NULL */
} ui_input_callbacks_t;

void ui_init(void);
void ui_switch_screen(ui_screen_id_t screen_id);
ui_screen_id_t ui_get_current_screen(void);
void ui_register_input_callbacks(ui_screen_id_t screen, const ui_input_callbacks_t *cbs);
void ui_unregister_input_callbacks(ui_screen_id_t screen);
void ui_dispatch_encoder_cw(void);
void ui_dispatch_encoder_ccw(void);
void ui_set_encoder_step(int step);
int  ui_get_encoder_step(void);
void ui_dispatch_encoder_press(void);
void ui_dispatch_encoder_long_press(void);
void ui_dispatch_settings_press(void);
void ui_dispatch_settings_long_press(void);
void ui_go_back(void);
void ui_push_screen(ui_screen_id_t screen_id);
void lvgl_lock(void);
void lvgl_unlock(void);
const char *ui_get_long_press_action(bool top_key);
void ui_show_long_press_hint(const char *name);
void ui_hide_long_press_hint(void);
