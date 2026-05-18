#include "ui_screen_bridge_scan.h"
#include "ui_screen_settings_buddy.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "service/tcp_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_BRIDGE_SCAN";

#define ROW_HEIGHT   22
#define VISIBLE_ROWS 8
#define INDENT       12
#define LIST_TOP     28
#define LIST_LEFT    4

/* ---- Display item types ---- */

typedef struct {
    bool is_header;     /* host:port row, not selectable */
    int  host_idx;      /* index into scan results */
    int  session_idx;   /* index within host's sessions (-1 for header) */
    char text[56];
} scan_item_t;

/* ---- Static state ---- */

static lv_obj_t *screen = NULL;
static lv_obj_t *row_labels[VISIBLE_ROWS];
static lv_obj_t *hint_label = NULL;
static lv_obj_t *scrollbar = NULL;

static scan_item_t items[64];
static int item_count = 0;
static int cursor = 0;       /* index into items[] of selected session */
static int scroll_off = 0;   /* first visible row index */
static int last_scan_count = -1;
static int total_hosts = 0;
static int total_sessions = 0;
static bool scan_done = false;  /* true after first scan completes */

/* ---- Helpers ---- */

static int next_selectable(int from, int dir)
{
    for (int i = from + dir; i >= 0 && i < item_count; i += dir) {
        if (!items[i].is_header) return i;
    }
    return from;
}

static void rebuild_items(void)
{
    item_count = 0;
    cursor = 0;
    scroll_off = 0;
    total_hosts = 0;
    total_sessions = 0;

    int count = tcp_service_get_scan_count();
    for (int h = 0; h < count && item_count < 60; h++) {
        const tcp_scan_result_t *r = tcp_service_get_scan_result(h);
        if (!r || r->session_count == 0) continue;

        /* Header row */
        items[item_count].is_header = true;
        items[item_count].host_idx = h;
        items[item_count].session_idx = -1;
        snprintf(items[item_count].text, sizeof(items[item_count].text),
                 "%s:%d", r->host, r->port);
        item_count++;
        total_hosts++;

        /* Session rows */
        for (int s = 0; s < r->session_count && item_count < 60; s++) {
            items[item_count].is_header = false;
            items[item_count].host_idx = h;
            items[item_count].session_idx = s;
            const char *name = r->sessions[s].project[0] ? r->sessions[s].project
                               : r->sessions[s].pairing_code;
            snprintf(items[item_count].text, sizeof(items[item_count].text),
                     "  %s", name);
            item_count++;
            total_sessions++;
        }
    }

    /* Place cursor on first selectable item */
    if (item_count > 0) {
        cursor = 0;
        if (items[0].is_header) cursor = next_selectable(0, 1);
    }
}

static void update_visible(void)
{
    lvgl_lock();

    /* Ensure cursor visible */
    if (cursor < scroll_off) scroll_off = cursor;
    if (cursor >= scroll_off + VISIBLE_ROWS) scroll_off = cursor - VISIBLE_ROWS + 1;
    if (scroll_off < 0) scroll_off = 0;
    int max_scroll = item_count - VISIBLE_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_off > max_scroll) scroll_off = max_scroll;

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        if (!row_labels[i]) continue;
        int idx = scroll_off + i;
        if (idx < item_count) {
            lv_obj_clear_flag(row_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(row_labels[i], items[idx].text);
            int x_off = items[idx].is_header ? LIST_LEFT : LIST_LEFT + INDENT;
            lv_obj_set_pos(row_labels[i], x_off, LIST_TOP + i * ROW_HEIGHT);

            if (items[idx].is_header) {
                lv_obj_set_style_text_color(row_labels[i], lv_color_hex(0x888888), 0);
            } else if (idx == cursor) {
                lv_obj_set_style_text_color(row_labels[i], lv_color_hex(0x00FF00), 0);
            } else {
                lv_obj_set_style_text_color(row_labels[i], lv_color_hex(0xCCCCCC), 0);
            }
        } else {
            lv_obj_add_flag(row_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Scrollbar */
    if (scrollbar && item_count > VISIBLE_ROWS) {
        int track_h = VISIBLE_ROWS * ROW_HEIGHT;
        int bar_h = (VISIBLE_ROWS * track_h) / item_count;
        if (bar_h < 12) bar_h = 12;
        int max_s = item_count - VISIBLE_ROWS;
        int bar_y = (max_s > 0) ? (scroll_off * (track_h - bar_h)) / max_s : 0;
        lv_obj_set_size(scrollbar, 3, bar_h);
        lv_obj_set_pos(scrollbar, 213, LIST_TOP + bar_y);
        lv_obj_clear_flag(scrollbar, LV_OBJ_FLAG_HIDDEN);
    } else if (scrollbar) {
        lv_obj_add_flag(scrollbar, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_unlock();
}

/* ---- Input callbacks ---- */

static void scan_on_encoder_cw(void)
{
    int next = next_selectable(cursor, 1);
    if (next != cursor) {
        cursor = next;
        update_visible();
    }
}

static void scan_on_encoder_ccw(void)
{
    int next = next_selectable(cursor, -1);
    if (next != cursor) {
        cursor = next;
        update_visible();
    }
}

static void scan_on_encoder_press(void)
{
    ui_go_back();
}

static void scan_on_encoder_long_press(void)
{
    ui_go_back();
}

static void scan_on_settings_press(void)
{
    /* If a session is selected, confirm and go back */
    if (cursor >= 0 && cursor < item_count && !items[cursor].is_header) {
        int hi = items[cursor].host_idx;
        int si = items[cursor].session_idx;
        const tcp_scan_result_t *r = tcp_service_get_scan_result(hi);
        if (r && si >= 0 && si < r->session_count) {
            ui_screen_settings_buddy_set_scan_result(
                r->host, r->port, r->sessions[si].pairing_code);
            ui_go_back();
            return;
        }
    }

    /* No results or scanning done — re-scan */
    if (scan_done && !tcp_service_is_scan_busy()) {
        tcp_service_scan();
        scan_done = false;
        last_scan_count = -1;
        item_count = 0;
        update_visible();
        if (hint_label) lv_label_set_text(hint_label, i18n(STR_SCANNING));
    }
}

/* ---- Public API ---- */

lv_obj_t *ui_screen_bridge_scan_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }

    memset(row_labels, 0, sizeof(row_labels));
    hint_label = NULL;
    scrollbar = NULL;
    last_scan_count = -1;
    item_count = 0;
    scan_done = false;

    /* Row labels (created first so title/hint render on top) */
    for (int i = 0; i < VISIBLE_ROWS; i++) {
        row_labels[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(row_labels[i], &custom_font_14, 0);
        lv_obj_set_pos(row_labels[i], LIST_LEFT, LIST_TOP + i * ROW_HEIGHT);
        lv_obj_set_size(row_labels[i], 210, ROW_HEIGHT);
        lv_label_set_long_mode(row_labels[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(row_labels[i], "");
    }

    /* Scrollbar */
    scrollbar = lv_obj_create(screen);
    lv_obj_remove_style_all(scrollbar);
    lv_obj_set_style_bg_color(scrollbar, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(scrollbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(scrollbar, 1, 0);
    lv_obj_add_flag(scrollbar, LV_OBJ_FLAG_HIDDEN);

    /* Title (after row_labels so it renders on top) */
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_SCAN));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    /* Hint (after row_labels so it renders on top) */
    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_SCANNING));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* Start scan */
    tcp_service_scan();

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = scan_on_encoder_cw,
        .on_encoder_ccw = scan_on_encoder_ccw,
        .on_encoder_press = scan_on_encoder_press,
        .on_encoder_long_press = scan_on_encoder_long_press,
        .on_settings_press = scan_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_BRIDGE_SCAN, &cbs);

    ESP_LOGI(TAG, "Bridge scan screen created");
    return screen;
}

void ui_screen_bridge_scan_refresh(void)
{
    if (tcp_service_is_scan_busy()) return;  /* still scanning, wait */
    if (scan_done) return;  /* already processed results */

    int count = tcp_service_get_scan_count();
    if (count == last_scan_count && count > 0) return;
    last_scan_count = count;
    scan_done = true;

    rebuild_items();
    update_visible();

    if (hint_label) {
        if (total_hosts > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), i18n(STR_FMT_SCAN_RESULT),
                     total_hosts, total_sessions);
            lv_label_set_text(hint_label, buf);
        } else {
            lv_label_set_text(hint_label, i18n(STR_NO_BRIDGE));
        }
    }
}
