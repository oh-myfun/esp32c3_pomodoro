#include "ui_screen_settings_debug.h"
#include "font_notosanssc.h"
#include "ui_manager.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_DEBUG";

#define LOG_LINES 24
#define LOG_LINE_MAX 80
#define LOG_AREA_Y 36
#define LOG_AREA_H 184  // 240 - 36(top) - 20(hint)
#define SCROLL_STEP 14  // ~12pt font line height

static char log_lines[LOG_LINES][LOG_LINE_MAX];
static int log_head = 0;
static int log_count = 0;
static SemaphoreHandle_t log_mutex = NULL;
static vprintf_like_t s_orig_vprintf = NULL;
static bool capturing = false;

static lv_obj_t *screen = NULL;
static lv_obj_t *log_cont = NULL;
static lv_obj_t *stats_label = NULL;
static lv_obj_t *log_label = NULL;
static lv_obj_t *hint_label = NULL;
static int scroll_y = 0;

static void log_ring_put(const char *line)
{
    if (!log_mutex) return;
    if (xSemaphoreTake(log_mutex, 0) != pdTRUE) return;
    strncpy(log_lines[log_head], line, LOG_LINE_MAX - 1);
    log_lines[log_head][LOG_LINE_MAX - 1] = '\0';
    log_head = (log_head + 1) % LOG_LINES;
    if (log_count < LOG_LINES) log_count++;
    xSemaphoreGive(log_mutex);
}

static int debug_vprintf(const char *fmt, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    char buf[LOG_LINE_MAX];
    int len = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    if (len > 0) {
        if (buf[len - 1] == '\n') buf[len - 1] = '\0';
        log_ring_put(buf);
    }

    if (s_orig_vprintf) {
        return s_orig_vprintf(fmt, args);
    }
    return len;
}

static void capture_start(void)
{
    if (capturing) return;
    if (!log_mutex) {
        log_mutex = xSemaphoreCreateMutex();
    }
    s_orig_vprintf = esp_log_set_vprintf(debug_vprintf);
    capturing = true;
    ESP_LOGI(TAG, "Log capture started");
}

static void capture_stop(void)
{
    if (!capturing || !s_orig_vprintf) return;
    esp_log_set_vprintf(s_orig_vprintf);
    s_orig_vprintf = NULL;
    capturing = false;
}

static char log_text[LOG_LINE_MAX * (LOG_LINES + 1)];

static void rebuild_log_text(void)
{
    if (!log_label || !log_mutex) return;
    if (xSemaphoreTake(log_mutex, 0) != pdTRUE) return;

    int total = log_count;
    int pos = 0;

    for (int i = 0; i < total && pos < (int)sizeof(log_text) - LOG_LINE_MAX; i++) {
        int line_idx = (log_count < LOG_LINES) ? i : ((log_head - total + i + LOG_LINES) % LOG_LINES);
        int written = snprintf(log_text + pos, sizeof(log_text) - pos, "%s\n", log_lines[line_idx]);
        if (written > 0) pos += written;
    }
    log_text[pos] = '\0';

    xSemaphoreGive(log_mutex);
    lv_label_set_text(log_label, log_text);
}

static void clamp_scroll(void)
{
    if (scroll_y > 0) scroll_y = 0;
    if (!log_label) return;
    int32_t content_h = lv_obj_get_self_height(log_label);
    int max_scroll = (content_h > LOG_AREA_H) ? -(content_h - LOG_AREA_H) : 0;
    if (scroll_y < max_scroll) scroll_y = max_scroll;
}

static void update_display(void)
{
    if (!stats_label) return;

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    int64_t uptime_s = esp_timer_get_time() / 1000000;

    char stats[200];
    snprintf(stats, sizeof(stats),
             "Heap: %u free / %u min\n"
             "Uptime: %lld s  Tasks: %u",
             (unsigned)info.total_free_bytes,
             (unsigned)info.minimum_free_bytes,
             uptime_s,
             (unsigned)uxTaskGetNumberOfTasks());
    lv_label_set_text(stats_label, stats);

    rebuild_log_text();
    clamp_scroll();
    if (log_label) {
        lv_obj_set_y(log_label, scroll_y);
    }
}

static void debug_on_encoder_cw(void)
{
    scroll_y += SCROLL_STEP;
    clamp_scroll();
    if (log_label) lv_obj_set_y(log_label, scroll_y);
}

static void debug_on_encoder_ccw(void)
{
    scroll_y -= SCROLL_STEP;
    clamp_scroll();
    if (log_label) lv_obj_set_y(log_label, scroll_y);
}

static void debug_exit(void)
{
    capture_stop();
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

static void debug_on_encoder_press(void) { debug_exit(); }
static void debug_on_encoder_long_press(void) { debug_exit(); }
static void debug_on_settings_press(void) { debug_exit(); }

lv_obj_t* ui_screen_settings_debug_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    stats_label = NULL;
    log_cont = NULL;
    log_label = NULL;
    hint_label = NULL;
    scroll_y = 0;

    stats_label = lv_label_create(screen);
    lv_obj_set_style_text_color(stats_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(stats_label, &lv_font_notosanssc_14, 0);
    lv_obj_align(stats_label, LV_ALIGN_TOP_LEFT, 4, 2);

    // Clipping container: fixed height, hides overflow
    log_cont = lv_obj_create(screen);
    lv_obj_remove_style_all(log_cont);
    lv_obj_set_size(log_cont, 232, LOG_AREA_H);
    lv_obj_set_pos(log_cont, 4, LOG_AREA_Y);
    lv_obj_set_style_bg_opa(log_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_clip_corner(log_cont, true, 0);
    lv_obj_clear_flag(log_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(log_cont, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    log_label = lv_label_create(log_cont);
    lv_obj_set_style_text_color(log_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(log_label, &lv_font_notosanssc_14, 0);
    lv_obj_set_width(log_label, 228);
    lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(log_label, 0, 0);

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, "Any key:back | Encoder:scroll");
    lv_obj_set_style_text_font(hint_label, &lv_font_notosanssc_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -2);

    capture_start();
    update_display();

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = debug_on_encoder_cw,
        .on_encoder_ccw = debug_on_encoder_ccw,
        .on_encoder_press = debug_on_encoder_press,
        .on_encoder_long_press = debug_on_encoder_long_press,
        .on_settings_press = debug_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_DEBUG, &cbs);

    ESP_LOGI(TAG, "Debug screen created");
    return screen;
}

void ui_screen_settings_debug_refresh(void)
{
    update_display();
}
