#include "ui_manager.h"
#include "ui_screen_main.h"
#include "ui_screen_pomodoro.h"
#include "ui_screen_settings.h"
#include "ui_screen_wifi.h"
#include "ui_text_input.h"
#include "ui_screen_settings_pomodoro.h"
#include "ui_screen_buddy.h"
#include "ui_screen_wifi_saved.h"
#include "ui_screen_settings_light.h"
#include "ui_screen_settings_buddy.h"
#include "ui_screen_settings_time.h"
#include "ui_screen_settings_system.h"
#include "ui_screen_settings_debug.h"
#include "ui_screen_bridge_scan.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "UI";

static lv_obj_t *screens[UI_SCREEN_COUNT];
static ui_screen_id_t current_screen = UI_SCREEN_MAIN;
static ui_input_callbacks_t input_callbacks[UI_SCREEN_COUNT];

static SemaphoreHandle_t lvgl_mutex = NULL;

#define UI_NAV_STACK_SIZE 8
static ui_screen_id_t nav_stack[UI_NAV_STACK_SIZE];
static int nav_depth = 0;

typedef lv_obj_t* (*screen_create_fn)(void);

static screen_create_fn lazy_creators[UI_SCREEN_COUNT];
static bool needs_rebuild[UI_SCREEN_COUNT];

static bool screen_is_disposable(ui_screen_id_t id)
{
    return id == UI_SCREEN_SETTINGS_POMODORO ||
           id == UI_SCREEN_SETTINGS_LIGHT ||
           id == UI_SCREEN_SETTINGS_BUDDY ||
           id == UI_SCREEN_SETTINGS_TIME ||
           id == UI_SCREEN_SETTINGS_SYSTEM ||
           id == UI_SCREEN_SETTINGS_DEBUG ||
           id == UI_SCREEN_BRIDGE_SCAN;
}

static void log_mem(const char *label)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "[MEM] %s free=%u  min_free=%u",
             label,
             (unsigned)info.total_free_bytes,
             (unsigned)info.minimum_free_bytes);
}

static void lvgl_lock_init(void)
{
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
}

void lvgl_lock(void)
{
    if (lvgl_mutex) {
        xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY);
    }
}

void lvgl_unlock(void)
{
    if (lvgl_mutex) {
        xSemaphoreGiveRecursive(lvgl_mutex);
    }
}

void ui_init(void)
{
    lvgl_lock_init();
    memset(screens, 0, sizeof(screens));
    memset(lazy_creators, 0, sizeof(lazy_creators));
    memset(needs_rebuild, 0, sizeof(needs_rebuild));
    memset(nav_stack, 0, sizeof(nav_stack));
    nav_depth = 0;

    // Core screens: create immediately
    screens[UI_SCREEN_MAIN] = ui_screen_main_create();
    screens[UI_SCREEN_POMODORO] = ui_screen_pomodoro_create();
    screens[UI_SCREEN_BUDDY] = ui_screen_buddy_create();
    screens[UI_SCREEN_SETTINGS] = ui_screen_settings_create();
    screens[UI_SCREEN_WIFI_LIST] = ui_screen_wifi_list_create();
    screens[UI_SCREEN_TEXT_INPUT] = ui_text_input_create();
    screens[UI_SCREEN_WIFI_SAVED] = ui_screen_wifi_saved_create();

    // Sub-setting screens: lazy load on first navigation
    lazy_creators[UI_SCREEN_SETTINGS_POMODORO] = ui_screen_settings_pomodoro_create;
    lazy_creators[UI_SCREEN_SETTINGS_LIGHT] = ui_screen_settings_light_create;
    lazy_creators[UI_SCREEN_SETTINGS_BUDDY] = ui_screen_settings_buddy_create;
    lazy_creators[UI_SCREEN_SETTINGS_TIME] = ui_screen_settings_time_create;
    lazy_creators[UI_SCREEN_SETTINGS_SYSTEM] = ui_screen_settings_system_create;
    lazy_creators[UI_SCREEN_SETTINGS_DEBUG] = ui_screen_settings_debug_create;
    lazy_creators[UI_SCREEN_BRIDGE_SCAN] = ui_screen_bridge_scan_create;

    lvgl_lock();
    lv_scr_load(screens[UI_SCREEN_MAIN]);
    lvgl_unlock();
    current_screen = UI_SCREEN_MAIN;

    log_mem("after ui_init");
}

static bool is_top_level(ui_screen_id_t id)
{
    return id == UI_SCREEN_MAIN || id == UI_SCREEN_POMODORO ||
           id == UI_SCREEN_BUDDY || id == UI_SCREEN_SETTINGS;
}

static void do_switch_screen(ui_screen_id_t screen_id, bool force_push)
{
    if (screen_id >= UI_SCREEN_COUNT) return;
    if (screen_id == current_screen) return;

    ui_screen_id_t old_screen = current_screen;

    /* Push current onto nav stack */
    if (force_push || !(is_top_level(old_screen) && is_top_level(screen_id))) {
        if (nav_depth < UI_NAV_STACK_SIZE) {
            nav_stack[nav_depth++] = old_screen;
        }
    }

    lvgl_lock();

    // Rebuild cleaned disposable screen (children removed, container kept)
    if (screens[screen_id] && needs_rebuild[screen_id] && lazy_creators[screen_id]) {
        ESP_LOGI(TAG, "Rebuilding screen %d", screen_id);
        lazy_creators[screen_id]();
        needs_rebuild[screen_id] = false;
    }

    // Lazy create on first access
    if (!screens[screen_id] && lazy_creators[screen_id]) {
        ESP_LOGI(TAG, "Lazy creating screen %d", screen_id);
        screens[screen_id] = lazy_creators[screen_id]();
        log_mem("after lazy create");
    }

    if (!screens[screen_id]) {
        lvgl_unlock();
        return;
    }

    lv_scr_load(screens[screen_id]);
    current_screen = screen_id;

    // Refresh data-driven screens on entry
    if (screen_id == UI_SCREEN_WIFI_SAVED) {
        ui_screen_wifi_saved_refresh();
    }

    // Clean old disposable screen: remove children to free memory
    if (screen_is_disposable(old_screen) && screens[old_screen]) {
        lv_obj_clean(screens[old_screen]);
        ui_unregister_input_callbacks(old_screen);
        needs_rebuild[old_screen] = true;
        log_mem("after clean");
    }

    lvgl_unlock();
}

void ui_switch_screen(ui_screen_id_t screen_id)
{
    do_switch_screen(screen_id, false);
}

void ui_push_screen(ui_screen_id_t screen_id)
{
    do_switch_screen(screen_id, true);
}

ui_screen_id_t ui_get_current_screen(void)
{
    return current_screen;
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

void ui_go_back(void)
{
    if (nav_depth <= 0) return;
    ui_screen_id_t prev = nav_stack[--nav_depth];

    lvgl_lock();

    /* Lazy create if needed */
    if (!screens[prev] && lazy_creators[prev]) {
        ESP_LOGI(TAG, "Lazy creating screen %d for go_back", prev);
        screens[prev] = lazy_creators[prev]();
    }

    if (!screens[prev]) {
        lvgl_unlock();
        return;
    }

    /* Rebuild if needed */
    if (needs_rebuild[prev] && lazy_creators[prev]) {
        ESP_LOGI(TAG, "Rebuilding screen %d for go_back", prev);
        lazy_creators[prev]();
        needs_rebuild[prev] = false;
    }

    ui_screen_id_t old_screen = current_screen;
    lv_scr_load(screens[prev]);
    current_screen = prev;

    /* Refresh data-driven screens on entry */
    if (prev == UI_SCREEN_WIFI_SAVED) {
        ui_screen_wifi_saved_refresh();
    }

    /* Clean old disposable screen */
    if (screen_is_disposable(old_screen) && screens[old_screen]) {
        lv_obj_clean(screens[old_screen]);
        ui_unregister_input_callbacks(old_screen);
        needs_rebuild[old_screen] = true;
    }

    lvgl_unlock();
}
