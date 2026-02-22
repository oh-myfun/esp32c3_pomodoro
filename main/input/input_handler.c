#include "input_handler.h"
#include "encoder.h"
#include "ui/ui_manager.h"
#include "ui/ui_screen_settings.h"
#include "ui/ui_screen_wifi.h"
#include "ui/ui_list.h"
#include "wifi_manager.h"
#include "pomodoro_engine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "INPUT_HANDLER";

static void encoder_handle_rotation(bool cw)
{
    ui_screen_id_t screen = ui_get_current_screen();
    settings_mode_t mode = ui_get_settings_mode();

    if (screen == UI_SCREEN_MAIN || screen == UI_SCREEN_POMODORO || 
        screen == UI_SCREEN_CHAT ||
        (screen == UI_SCREEN_SETTINGS && mode == SETTINGS_MODE_IDLE)) {
        int target_screen;
        if (cw) {
            if (screen == UI_SCREEN_MAIN) target_screen = UI_SCREEN_POMODORO;
            else if (screen == UI_SCREEN_POMODORO) target_screen = UI_SCREEN_CHAT;
            else if (screen == UI_SCREEN_CHAT) target_screen = UI_SCREEN_SETTINGS;
            else target_screen = UI_SCREEN_MAIN;
        } else {
            if (screen == UI_SCREEN_MAIN) target_screen = UI_SCREEN_SETTINGS;
            else if (screen == UI_SCREEN_SETTINGS) target_screen = UI_SCREEN_CHAT;
            else if (screen == UI_SCREEN_CHAT) target_screen = UI_SCREEN_POMODORO;
            else target_screen = UI_SCREEN_MAIN;
        }
        ui_switch_screen(target_screen);
    }
    else if (screen == UI_SCREEN_WIFI_LIST) {
        lv_obj_t *list = ui_screen_wifi_list_get_list();
        if (cw) ui_list_nav_next(list);
        else ui_list_nav_prev(list);
    }
    else if (screen == UI_SCREEN_PASSWORD_INPUT) {
        if (cw) ui_password_input_char_next();
        else ui_password_input_char_prev();
    }
    else if (screen == UI_SCREEN_SETTINGS && mode == SETTINGS_MODE_SELECT) {
        if (cw) ui_settings_select_next();
        else ui_settings_select_prev();
    }
    else if (screen == UI_SCREEN_SETTINGS && mode == SETTINGS_MODE_ADJUST) {
        if (cw) ui_settings_adjust_up();
        else ui_settings_adjust_down();
    }
    else if (screen == UI_SCREEN_SETTINGS_POMODORO) {
        if (cw) ui_settings_adjust_up();
        else ui_settings_adjust_down();
    }
}

static void encoder_handle_press(void)
{
    ui_screen_id_t screen = ui_get_current_screen();
    settings_mode_t mode = ui_get_settings_mode();

    if (screen == UI_SCREEN_POMODORO) {
        pomodoro_engine_stop();
    }
    else if (screen == UI_SCREEN_WIFI_LIST) {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
    else if (screen == UI_SCREEN_PASSWORD_INPUT) {
        ui_password_input_cancel();
        ui_switch_screen(UI_SCREEN_WIFI_LIST);
        ui_screen_wifi_list_refresh();
    }
    else if (screen == UI_SCREEN_SETTINGS) {
        if (mode == SETTINGS_MODE_ADJUST) {
            ui_exit_settings();
        } else if (mode == SETTINGS_MODE_SELECT) {
            ui_exit_settings();
        }
    }
    else if (screen == UI_SCREEN_SETTINGS_POMODORO) {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

static void settings_button_handle(void)
{
    ui_screen_id_t screen = ui_get_current_screen();
    settings_mode_t mode = ui_get_settings_mode();

    if (screen == UI_SCREEN_WIFI_LIST) {
        int count = wifi_manager_get_scan_count();
        int selected = ui_screen_wifi_list_get_selected();
        if (count > 0) {
            wifi_scan_result_t *result = wifi_manager_get_scan_result(selected);
            if (result) {
                ui_password_input_start(result->ssid);
            }
        }
    }
    else if (screen == UI_SCREEN_PASSWORD_INPUT) {
        ui_password_input_add_char();
    }
    else if (screen == UI_SCREEN_POMODORO) {
        pomodoro_state_t state = pomodoro_engine_get_state();
        if (state.phase == 0) {
            pomodoro_engine_start();
        } else if (state.phase == 4) {
            pomodoro_engine_resume();
        } else {
            pomodoro_engine_pause();
        }
    }
    else if (screen == UI_SCREEN_SETTINGS) {
        if (mode == SETTINGS_MODE_IDLE) {
            ui_enter_settings();
        } else if (mode == SETTINGS_MODE_SELECT) {
            int item = ui_screen_settings_get_current_item();
            if (item == 4) {
                wifi_manager_scan_start();
                ui_switch_screen(UI_SCREEN_WIFI_LIST);
            } else if (item == 3) {
                ui_switch_screen(UI_SCREEN_SETTINGS_POMODORO);
            } else {
                ui_settings_enter_adjust();
            }
        } else if (mode == SETTINGS_MODE_ADJUST) {
            ui_exit_settings();
        }
    }
    else if (screen == UI_SCREEN_SETTINGS_POMODORO) {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

void input_handler_init(void)
{
    encoder_init();
    ESP_LOGI(TAG, "Input handler initialized");
}

void input_handler_task(void *arg)
{
    ESP_LOGI(TAG, "Input handler task started");

    while (1) {
        ec11_event_t event = encoder_get_event();

        if (event != EC11_EVENT_NONE) {
            switch (event) {
                case EC11_EVENT_CW:
                    encoder_handle_rotation(true);
                    break;
                case EC11_EVENT_CCW:
                    encoder_handle_rotation(false);
                    break;
                case EC11_EVENT_PRESS:
                    encoder_handle_press();
                    break;
                default:
                    break;
            }
        }

        if (settings_button_get_event()) {
            settings_button_handle();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
