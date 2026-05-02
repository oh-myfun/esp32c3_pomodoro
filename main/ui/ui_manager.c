#include "ui_manager.h"
#include "ui_screen_main.h"
#include "ui_screen_pomodoro.h"
#include "ui_screen_settings.h"
#include "ui_screen_wifi.h"
#include "ui_screen_chat.h"
#include "ui_screen_settings_pomodoro.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "UI";

static lv_obj_t *screens[UI_SCREEN_COUNT];
static ui_screen_id_t current_screen = UI_SCREEN_MAIN;
static settings_mode_t settings_mode = SETTINGS_MODE_IDLE;
static ui_input_callbacks_t input_callbacks[UI_SCREEN_COUNT];

static SemaphoreHandle_t lvgl_mutex = NULL;

void lvgl_lock_init(void)
{
    lvgl_mutex = xSemaphoreCreateMutex();
}

void lvgl_lock(void)
{
    if (lvgl_mutex) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    }
}

void lvgl_unlock(void)
{
    if (lvgl_mutex) {
        xSemaphoreGive(lvgl_mutex);
    }
}

static void update_settings_display(void)
{
    if (current_screen != UI_SCREEN_SETTINGS) return;
    ui_screen_settings_update(ui_screen_settings_get_values(), ui_screen_settings_get_current_item(), ui_screen_settings_get_mode());
}

void ui_init(void)
{
    lvgl_lock_init();

    screens[UI_SCREEN_MAIN] = ui_screen_main_create();
    screens[UI_SCREEN_POMODORO] = ui_screen_pomodoro_create();
    screens[UI_SCREEN_CHAT] = ui_screen_chat_create();
    screens[UI_SCREEN_SETTINGS] = ui_screen_settings_create();
    screens[UI_SCREEN_SETTINGS_POMODORO] = ui_screen_settings_pomodoro_create();
    screens[UI_SCREEN_WIFI_LIST] = ui_screen_wifi_list_create();
    screens[UI_SCREEN_PASSWORD_INPUT] = ui_screen_password_create();

    lvgl_lock();
    lv_scr_load(screens[UI_SCREEN_MAIN]);
    lvgl_unlock();
    current_screen = UI_SCREEN_MAIN;
    settings_mode = SETTINGS_MODE_IDLE;

    ESP_LOGI(TAG, "UI initialized: %d screens", UI_SCREEN_COUNT);
}

void ui_switch_screen(ui_screen_id_t screen_id)
{
    if (screen_id >= UI_SCREEN_COUNT) return;
    if (screen_id == current_screen) return;

    memset(&input_callbacks[current_screen], 0, sizeof(ui_input_callbacks_t));

    lvgl_lock();
    lv_scr_load(screens[screen_id]);
    lvgl_unlock();
    current_screen = screen_id;

    if (screen_id == UI_SCREEN_SETTINGS) {
        update_settings_display();
    }
}

ui_screen_id_t ui_get_current_screen(void)
{
    return current_screen;
}

settings_mode_t ui_get_settings_mode(void)
{
    return ui_screen_settings_get_mode();
}

void ui_register_input_callbacks(ui_screen_id_t screen, const ui_input_callbacks_t *cbs)
{
    if (screen >= UI_SCREEN_COUNT || !cbs) return;
    memcpy(&input_callbacks[screen], cbs, sizeof(ui_input_callbacks_t));
}

void ui_unregister_input_callbacks(ui_screen_id_t screen)
{
    if (screen >= UI_SCREEN_COUNT) return;
    memset(&input_callbacks[screen], 0, sizeof(ui_input_callbacks_t));
}

void ui_dispatch_encoder_cw(void)
{
    if (input_callbacks[current_screen].on_encoder_cw) {
        input_callbacks[current_screen].on_encoder_cw();
    }
}

void ui_dispatch_encoder_ccw(void)
{
    if (input_callbacks[current_screen].on_encoder_ccw) {
        input_callbacks[current_screen].on_encoder_ccw();
    }
}

void ui_dispatch_encoder_press(void)
{
    if (input_callbacks[current_screen].on_encoder_press) {
        input_callbacks[current_screen].on_encoder_press();
    }
}

void ui_dispatch_encoder_long_press(void)
{
    if (input_callbacks[current_screen].on_encoder_long_press) {
        input_callbacks[current_screen].on_encoder_long_press();
    }
}

void ui_dispatch_settings_press(void)
{
    if (input_callbacks[current_screen].on_settings_press) {
        input_callbacks[current_screen].on_settings_press();
    }
}

void ui_update_time(void)
{
    ui_screen_main_update_time();
}

void ui_update_temp(float temp)
{
    ui_screen_main_update_temp(temp);
}

void ui_update_humidity(float humidity)
{
    ui_screen_main_update_humidity(humidity);
}

void ui_update_wifi_status(const char *status)
{
    ui_screen_main_update_wifi_status(status);
}

void ui_update_wifi_status_ex(const char *status, uint32_t color)
{
    ui_screen_main_update_wifi_status_ex(status, color);
}

void ui_pomodoro_update_time(uint32_t remaining_seconds)
{
    ui_screen_pomodoro_update_time(remaining_seconds);
}

void ui_pomodoro_update_phase(const char *phase)
{
    ui_screen_pomodoro_update_phase(phase);
}

void ui_pomodoro_update_completed(uint32_t count)
{
    ui_screen_pomodoro_update_completed(count);
}

void ui_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed)
{
    ui_screen_pomodoro_update_state(phase, remaining_seconds, completed);
}

void ui_enter_settings(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

void ui_exit_settings(void)
{
    ui_switch_screen(UI_SCREEN_MAIN);
}

void ui_settings_select_next(void)
{
    if (current_screen == UI_SCREEN_SETTINGS) {
        ui_screen_settings_select_next();
    }
}

void ui_settings_select_prev(void)
{
    if (current_screen == UI_SCREEN_SETTINGS) {
        ui_screen_settings_select_prev();
    }
}

void ui_settings_enter_adjust(void)
{
    if (current_screen == UI_SCREEN_SETTINGS) {
        ui_screen_settings_enter_adjust();
    }
}

void ui_settings_adjust_up(void)
{
    if (current_screen == UI_SCREEN_SETTINGS) {
        ui_screen_settings_adjust_up();
    } else if (current_screen == UI_SCREEN_SETTINGS_POMODORO) {
        ui_screen_settings_pomodoro_adjust_up();
    }
}

void ui_settings_adjust_down(void)
{
    if (current_screen == UI_SCREEN_SETTINGS) {
        ui_screen_settings_adjust_down();
    } else if (current_screen == UI_SCREEN_SETTINGS_POMODORO) {
        ui_screen_settings_pomodoro_adjust_down();
    }
}
