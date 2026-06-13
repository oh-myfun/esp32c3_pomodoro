#include "ui_manager.h"
#include "custom_font.h"
#include "ui_screen_main.h"
#include "ui_screen_pomodoro.h"
#include "ui_screen_settings.h"
#include "ui_screen_wifi.h"
#include "ui_text_input.h"
#include "ui_screen_settings_pomodoro.h"
#include "ui_screen_settings_alarm.h"
#include "ui_screen_buddy.h"
#include "ui_screen_wifi_saved.h"
#include "ui_screen_settings_light.h"
#include "ui_screen_settings_buddy.h"
#include "ui_screen_settings_time.h"
#include "ui_screen_settings_system.h"
#include "ui_screen_settings_sound.h"
#include "ui_screen_settings_light_demo.h"
#include "ui_screen_settings_sound_demo.h"
#include "ui_screen_settings_debug.h"
#include "ui_screen_bridge_scan.h"
#include "ui_screen_sensor.h"
#include "ui_screen_settings_sensor.h"
#include "ui_screen_pressure_info.h"
#include "ui_screen_settings.h"
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
           id == UI_SCREEN_SETTINGS_ALARM ||
           id == UI_SCREEN_SETTINGS_LIGHT ||
           id == UI_SCREEN_SETTINGS_BUDDY ||
           id == UI_SCREEN_SETTINGS_TIME ||
           id == UI_SCREEN_SETTINGS_SYSTEM ||
           id == UI_SCREEN_SETTINGS_SOUND  ||
           id == UI_SCREEN_SETTINGS_LIGHT_DEMO ||
           id == UI_SCREEN_SETTINGS_SOUND_DEMO ||
           id == UI_SCREEN_SETTINGS_DEBUG ||
           id == UI_SCREEN_BRIDGE_SCAN ||
           id == UI_SCREEN_SETTINGS_SENSOR ||
           id == UI_SCREEN_PRESSURE_INFO;
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
    lazy_creators[UI_SCREEN_SETTINGS_ALARM] = ui_screen_settings_alarm_create;
    lazy_creators[UI_SCREEN_SETTINGS_LIGHT] = ui_screen_settings_light_create;
    lazy_creators[UI_SCREEN_SETTINGS_BUDDY] = ui_screen_settings_buddy_create;
    lazy_creators[UI_SCREEN_SETTINGS_TIME] = ui_screen_settings_time_create;
    lazy_creators[UI_SCREEN_SETTINGS_SYSTEM] = ui_screen_settings_system_create;
    lazy_creators[UI_SCREEN_SETTINGS_SOUND]  = ui_screen_settings_sound_create;
    lazy_creators[UI_SCREEN_SETTINGS_LIGHT_DEMO] = ui_screen_settings_light_demo_create;
    lazy_creators[UI_SCREEN_SETTINGS_SOUND_DEMO] = ui_screen_settings_sound_demo_create;
    lazy_creators[UI_SCREEN_SETTINGS_DEBUG] = ui_screen_settings_debug_create;
    lazy_creators[UI_SCREEN_BRIDGE_SCAN] = ui_screen_bridge_scan_create;
    lazy_creators[UI_SCREEN_SENSOR] = ui_screen_sensor_create;
    lazy_creators[UI_SCREEN_SETTINGS_SENSOR] = ui_screen_settings_sensor_create;
    lazy_creators[UI_SCREEN_PRESSURE_INFO] = ui_screen_pressure_info_create;

    lvgl_lock();
    lv_scr_load(screens[UI_SCREEN_MAIN]);
    lvgl_unlock();
    current_screen = UI_SCREEN_MAIN;

    log_mem("after ui_init");
}

static bool is_top_level(ui_screen_id_t id)
{
    return id == UI_SCREEN_MAIN || id == UI_SCREEN_POMODORO ||
           id == UI_SCREEN_BUDDY || id == UI_SCREEN_SETTINGS ||
           id == UI_SCREEN_SENSOR;
}

static void do_switch_screen(ui_screen_id_t screen_id, bool force_push)
{
    if (screen_id >= UI_SCREEN_COUNT) return;
    if (screen_id == current_screen) return;

    ui_screen_id_t old_screen = current_screen;
    ESP_LOGI(TAG, "switch %d -> %d (push=%d, depth=%d)", old_screen, screen_id, force_push, nav_depth);

    /* Push current onto nav stack */
    if (force_push || !(is_top_level(old_screen) && is_top_level(screen_id))) {
        if (nav_depth < UI_NAV_STACK_SIZE) {
            nav_stack[nav_depth++] = old_screen;
        }
    }

    lvgl_lock();

    /* Clean old disposable screen BEFORE creating new one to reduce peak heap
     * (old + new simultaneously can exhaust heap on ESP32-C3). Visual transition
     * is still atomic because we hold lvgl_lock through the whole switch. */
    if (old_screen != screen_id && screen_is_disposable(old_screen) && screens[old_screen]) {
        lv_obj_clean(screens[old_screen]);
        ui_unregister_input_callbacks(old_screen);
        needs_rebuild[old_screen] = true;
        log_mem("after pre-clean");
    }

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
    if (screen_id == UI_SCREEN_SETTINGS) {
        ui_screen_settings_refresh();
    }
    if (screen_id == UI_SCREEN_POMODORO) {
        ui_screen_pomodoro_refresh();
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

static int s_encoder_step = 1;

void ui_set_encoder_step(int step)
{
    s_encoder_step = step;
}

int ui_get_encoder_step(void)
{
    return s_encoder_step;
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

void ui_dispatch_settings_long_press(void)
{
    if (input_callbacks[current_screen].on_settings_long_press) {
        input_callbacks[current_screen].on_settings_long_press();
    }
}

const char *ui_get_long_press_action(bool top_key)
{
    if (input_callbacks[current_screen].on_long_press_hint) {
        return input_callbacks[current_screen].on_long_press_hint(top_key);
    }
    return NULL;
}

void ui_go_back(void)
{
    if (nav_depth <= 0) return;
    ui_screen_id_t prev = nav_stack[--nav_depth];

    lvgl_lock();

    ui_screen_id_t old_screen = current_screen;

    /* Clean current disposable screen BEFORE rebuilding/creating target to reduce peak heap */
    if (old_screen != prev && screen_is_disposable(old_screen) && screens[old_screen]) {
        lv_obj_clean(screens[old_screen]);
        ui_unregister_input_callbacks(old_screen);
        needs_rebuild[old_screen] = true;
        log_mem("after go_back pre-clean");
    }

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

    lv_scr_load(screens[prev]);
    current_screen = prev;

    /* Refresh data-driven screens on entry */
    if (prev == UI_SCREEN_WIFI_SAVED) {
        ui_screen_wifi_saved_refresh();
    }
    if (prev == UI_SCREEN_SETTINGS) {
        ui_screen_settings_refresh();
    }
    if (prev == UI_SCREEN_POMODORO) {
        ui_screen_pomodoro_refresh();
    }

    lvgl_unlock();
}

/* --- Long press progress bar overlay --- */

static lv_obj_t *lp_bg = NULL;
static lv_obj_t *lp_fill = NULL;
static lv_obj_t *lp_label = NULL;
static bool lp_visible = false;

#define LP_HEIGHT 18
#define LP_WIDTH  240

static void lp_fill_anim_cb(void *var, int32_t v)
{
    lv_obj_set_width((lv_obj_t *)var, v);
}

static void lp_create(void)
{
    if (lp_bg) return;

    lv_obj_t *layer = lv_layer_top();

    lp_bg = lv_obj_create(layer);
    lv_obj_remove_style_all(lp_bg);
    lv_obj_set_size(lp_bg, LP_WIDTH, LP_HEIGHT);
    lv_obj_align(lp_bg, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(lp_bg, lv_color_hex(0x0d0d0d), 0);
    lv_obj_set_style_bg_opa(lp_bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(lp_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(lp_bg, LV_OBJ_FLAG_HIDDEN);

    lp_fill = lv_obj_create(lp_bg);
    lv_obj_remove_style_all(lp_fill);
    lv_obj_set_pos(lp_fill, 0, 0);
    lv_obj_set_size(lp_fill, 0, LP_HEIGHT);
    lv_obj_set_style_bg_color(lp_fill, lv_color_hex(0x00AA00), 0);
    lv_obj_set_style_bg_opa(lp_fill, LV_OPA_70, 0);
    lv_obj_clear_flag(lp_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lp_label = lv_label_create(lp_bg);
    lv_label_set_text(lp_label, "");
    lv_obj_set_style_text_color(lp_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lp_label, &custom_font_14, 0);
    lv_obj_align(lp_label, LV_ALIGN_CENTER, 0, 0);
}

void ui_show_long_press_hint(const char *name)
{
    lp_create();

    lv_label_set_text(lp_label, name);

    if (lp_visible) {
        return;
    }

    lv_obj_set_width(lp_fill, 0);
    lv_obj_clear_flag(lp_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(lp_bg);

    lv_anim_del(lp_fill, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lp_fill);
    lv_anim_set_exec_cb(&a, lp_fill_anim_cb);
    lv_anim_set_values(&a, 0, LP_WIDTH);
    lv_anim_set_time(&a, 1500);
    lv_anim_start(&a);

    lp_visible = true;
}

void ui_hide_long_press_hint(void)
{
    if (!lp_visible || !lp_bg) return;

    lv_anim_del(lp_fill, NULL);
    lv_obj_add_flag(lp_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(lp_fill, 0);
    lp_visible = false;
}
