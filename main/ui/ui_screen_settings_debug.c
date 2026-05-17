#include "ui_screen_settings_debug.h"
#include "custom_font.h"
#include "i18n.h"
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
#define SCROLL_STEP 18

static char log_lines[LOG_LINES][LOG_LINE_MAX];
static int log_head = 0;
static int log_count = 0;
static SemaphoreHandle_t log_mutex = NULL;
static vprintf_like_t s_orig_vprintf = NULL;
static bool capturing = false;

static lv_obj_t *screen = NULL;
static lv_obj_t *title_label = NULL;  /* stats or "Symbol Preview" */
static lv_obj_t *cont = NULL;         /* shared clipping container */
static lv_obj_t *content_label = NULL;/* shared content label */
static lv_obj_t *hint_label = NULL;
static int scroll_y = 0;

typedef enum { DEBUG_VIEW_LOG, DEBUG_VIEW_SYMBOLS } debug_view_t;
static debug_view_t current_view = DEBUG_VIEW_LOG;
static bool user_scrolled = false;

/* Rebuild log text from ring buffer into a provided buffer */
static void rebuild_log_text(char *buf, int buf_size)
{
    if (!log_mutex) return;
    if (xSemaphoreTake(log_mutex, 0) != pdTRUE) return;

    int total = log_count;
    int pos = 0;

    for (int i = 0; i < total && pos < buf_size - LOG_LINE_MAX - 12; i++) {
        int line_idx = (log_count < LOG_LINES) ? i : ((log_head - total + i + LOG_LINES) % LOG_LINES);
        const char *line = log_lines[line_idx];

        /* Color code by log level prefix: E=red, W=orange, I=default */
        char level = line[0];
        int written;
        if (level == 'E') {
            written = snprintf(buf + pos, buf_size - pos, "#FF4444 %s#\n", line);
        } else if (level == 'W') {
            written = snprintf(buf + pos, buf_size - pos, "#FFAA00 %s#\n", line);
        } else {
            written = snprintf(buf + pos, buf_size - pos, "%s\n", line);
        }
        if (written > 0) pos += written;
    }
    buf[pos] = '\0';

    xSemaphoreGive(log_mutex);
}

/* Only symbols that Maple Mono CN actually has glyphs for */
static const char symbol_text[] =
    "=== Arrows ===\n"
    "←↑→↓ ↖↗↘↙\n"
    "←→↑↓ ⇦⇧⇨⇩\n"
    "↝↠↢↣↤↥\n"
    "↩↪↭ ↰↱\n"
    "➔➜➝\n"
    "\n"
    "=== Geometric ===\n"
    "■□ ▲△ ▼▽\n"
    "◆◇ ○● ◉◊\n"
    "◀▶ ◁▷ ▂▃▄▅\n"
    "\n"
    "=== Check/Cross ===\n"
    "✓✔ ✗✘ ☑☒\n"
    "\n"
    "=== Misc Symbols ===\n"
    "♥ ⚠⚡ ☰\n"
    "\n"
    "=== Math ===\n"
    "∀∂∃ ∅∈\n"
    "∏∑ √∞\n"
    "∧∨ ∩∪\n"
    "∫ ∴∵\n"
    "≈≠ ≤≥\n"
    "⊂⊃ ⊕⊗⊙\n"
    "\n"
    "=== Latin/Symbols ===\n"
    "±×÷ §©®\n"
    "°‰ ′″\n"
    "–— ―\n"
    "… †‡ •\n"
    "℃ €£¥\n"
    "\n"
    "=== CJK Punct ===\n"
    "、。 〃々 〆\n"
    "〈〉 《》\n"
    "「」 『』\n"
    "【】 〔〕\n"
    "〖〗 〘〙\n"
    "\n"
    "=== Blocks ===\n"
    "█▉▊ ▋▌▍\n"
    "░▒▓\n"
    "\n"
    "=== Box Drawing ===\n"
    "┌┐┘└\n"
    "├┤┬┴┼\n"
    "─│ ═║\n"
    "\n"
    "=== Emoji (NotoEmoji) ===\n"
    "🍅🐱💡📶🕐⚙🔧\n"
    "🎯☕🏖⏸⏹\n"
    "😴😊💼⚠🎉😵❤\n"
    "🔊✅❌🔄✏🔗\n"
    "🔓🔒🗑🔍🔑\n";

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

static void clamp_scroll(void)
{
    if (scroll_y > 0) scroll_y = 0;
    if (!content_label) return;
    int32_t content_h = lv_obj_get_self_height(content_label);
    int max_scroll = (content_h > LOG_AREA_H) ? -(content_h - LOG_AREA_H) : 0;
    if (scroll_y < max_scroll) scroll_y = max_scroll;
}

static void scroll_to_bottom(void)
{
    if (!content_label) return;
    int32_t content_h = lv_obj_get_self_height(content_label);
    if (content_h > LOG_AREA_H) {
        scroll_y = -(content_h - LOG_AREA_H);
    } else {
        scroll_y = 0;
    }
}

static void show_log_view(void)
{
    current_view = DEBUG_VIEW_LOG;

    /* Update title with stats */
    if (title_label) {
        multi_heap_info_t info;
        heap_caps_get_info(&info, MALLOC_CAP_8BIT);
        int64_t uptime_s = esp_timer_get_time() / 1000000;
        char stats[200];
        snprintf(stats, sizeof(stats),
                 "Heap: %u free / %u min | Up: %llds | Tasks: %u",
                 (unsigned)info.total_free_bytes,
                 (unsigned)info.minimum_free_bytes,
                 uptime_s,
                 (unsigned)uxTaskGetNumberOfTasks());
        lv_label_set_text(title_label, stats);
        lv_obj_set_style_text_color(title_label, lv_color_hex(0x00FF00), 0);
    }

    /* Fill content with log text */
    if (content_label) {
        static char log_text[(LOG_LINE_MAX + 12) * (LOG_LINES + 1)];
        rebuild_log_text(log_text, sizeof(log_text));
        lv_label_set_text(content_label, log_text);

        if (!user_scrolled) {
            scroll_to_bottom();
        } else {
            clamp_scroll();
        }
        lv_obj_set_y(content_label, scroll_y);
    }

    if (hint_label) {
        lv_label_set_text(hint_label, i18n(STR_H_ANY_KEY_BACK_ENCODER_SCROLL));
    }
}

static void show_symbol_view(void)
{
    current_view = DEBUG_VIEW_SYMBOLS;
    scroll_y = 0;
    user_scrolled = false;

    if (title_label) {
        lv_label_set_text(title_label, "Symbol Preview");
        lv_obj_set_style_text_color(title_label, lv_color_hex(0x00FFFF), 0);
    }

    if (content_label) {
        lv_label_set_text(content_label, symbol_text);
        lv_obj_set_y(content_label, 0);
    }

    if (hint_label) {
        lv_label_set_text(hint_label, "TOP:log|Encoder:scroll|SIDE:back");
    }
}

static void debug_on_encoder_cw(void)
{
    if (current_view == DEBUG_VIEW_SYMBOLS) {
        scroll_y += SCROLL_STEP;
        clamp_scroll();
        if (content_label) lv_obj_set_y(content_label, scroll_y);
        return;
    }
    scroll_y += SCROLL_STEP;
    clamp_scroll();
    if (scroll_y >= 0) user_scrolled = false;
    if (content_label) lv_obj_set_y(content_label, scroll_y);
}

static void debug_on_encoder_ccw(void)
{
    if (current_view == DEBUG_VIEW_SYMBOLS) {
        scroll_y -= SCROLL_STEP;
        clamp_scroll();
        if (content_label) lv_obj_set_y(content_label, scroll_y);
        return;
    }
    user_scrolled = true;
    scroll_y -= SCROLL_STEP;
    clamp_scroll();
    if (content_label) lv_obj_set_y(content_label, scroll_y);
}

static void debug_exit(void)
{
    capture_stop();
    current_view = DEBUG_VIEW_LOG;
    ui_go_back();
}

static void debug_on_encoder_press(void) { debug_exit(); }
static void debug_on_encoder_long_press(void) { debug_exit(); }

static void debug_on_settings_press(void)
{
    if (current_view == DEBUG_VIEW_LOG) {
        show_symbol_view();
    } else {
        show_log_view();
    }
}

lv_obj_t* ui_screen_settings_debug_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    title_label = NULL;
    cont = NULL;
    content_label = NULL;
    hint_label = NULL;
    scroll_y = 0;
    user_scrolled = false;
    current_view = DEBUG_VIEW_LOG;

    title_label = lv_label_create(screen);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(title_label, &custom_font_14, 0);
    lv_obj_set_style_text_line_space(title_label, -4, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 4, 2);

    /* Shared clipping container */
    cont = lv_obj_create(screen);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 232, LOG_AREA_H);
    lv_obj_set_pos(cont, 4, LOG_AREA_Y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* Shared content label */
    content_label = lv_label_create(cont);
    lv_obj_set_style_text_color(content_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(content_label, &custom_font_14, 0);
    lv_obj_set_style_text_line_space(content_label, -4, 0);
    lv_obj_set_width(content_label, 228);
    lv_label_set_long_mode(content_label, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(content_label, true);
    lv_obj_set_pos(content_label, 0, 0);

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_ANY_KEY_BACK_ENCODER_SCROLL));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -2);

    capture_start();
    show_log_view();

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
    if (current_view == DEBUG_VIEW_LOG) {
        show_log_view();
    }
}
