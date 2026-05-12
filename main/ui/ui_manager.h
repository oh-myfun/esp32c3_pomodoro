#pragma once

#include "lvgl.h"

typedef enum {
    UI_SCREEN_MAIN = 0,
    UI_SCREEN_POMODORO,
    UI_SCREEN_BUDDY,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_SETTINGS_POMODORO,
    UI_SCREEN_WIFI_LIST,
    UI_SCREEN_PASSWORD_INPUT,
    UI_SCREEN_WIFI_SAVED,
    UI_SCREEN_SETTINGS_LIGHT,
    UI_SCREEN_SETTINGS_BUDDY,
    UI_SCREEN_SETTINGS_TIME,
    UI_SCREEN_SETTINGS_SYSTEM,
    UI_SCREEN_SETTINGS_DEBUG,
    UI_SCREEN_COUNT
} ui_screen_id_t;

typedef struct {
    void (*on_encoder_cw)(void);
    void (*on_encoder_ccw)(void);
    void (*on_encoder_press)(void);
    void (*on_encoder_long_press)(void);
    void (*on_settings_press)(void);
} ui_input_callbacks_t;

void ui_init(void);
void ui_switch_screen(ui_screen_id_t screen_id);
ui_screen_id_t ui_get_current_screen(void);
void ui_register_input_callbacks(ui_screen_id_t screen, const ui_input_callbacks_t *cbs);
void ui_unregister_input_callbacks(ui_screen_id_t screen);
void ui_dispatch_encoder_cw(void);
void ui_dispatch_encoder_ccw(void);
void ui_dispatch_encoder_press(void);
void ui_dispatch_encoder_long_press(void);
void ui_dispatch_settings_press(void);
void lvgl_lock(void);
void lvgl_unlock(void);
