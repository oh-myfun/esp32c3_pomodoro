#include "ui_screen_buddy.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "buddy/buddy.h"
#include "buddy/buddy_render.h"
#include "service/tcp_service.h"
#include "esp_log.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_BUDDY";

/* ----------------------------------------------------------------
 * Display modes
 * ---------------------------------------------------------------- */
typedef enum {
    MODE_NORMAL    = 0,
    MODE_ATTENTION,
} buddy_display_mode_t;

/* ----------------------------------------------------------------
 * Static LVGL objects — Normal mode
 * ---------------------------------------------------------------- */
static lv_obj_t *screen        = NULL;

/* Top bar */
static lv_obj_t *conn_label    = NULL;  /* ✓session or ✗ */
static lv_obj_t *state_label   = NULL;

/* Center — custom-drawn pet canvas */
static lv_obj_t *pet_canvas    = NULL;

/* Below pet */
static lv_obj_t *heart_label   = NULL;
static lv_obj_t *stats_label   = NULL;

/* Bottom hint */
static lv_obj_t *nav_hint      = NULL;

/* ----------------------------------------------------------------
 * Static LVGL objects — ATTENTION mode overlay
 * ---------------------------------------------------------------- */
static lv_obj_t *attn_container = NULL;
static lv_obj_t *attn_tool     = NULL;
static lv_obj_t *attn_cmd      = NULL;
static lv_obj_t *attn_canvas   = NULL;
#define OPT_VISIBLE 5
#define OPT_ROW_H   22
#define OPT_Y_START 72   /* bottom-aligned: 182 - 5*22 = 72 */
static lv_obj_t *attn_opt_labels[OPT_VISIBLE] = {NULL};
static lv_obj_t *attn_scrollbar = NULL;
static lv_obj_t *attn_desc     = NULL;

/* ----------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------- */
static buddy_display_mode_t display_mode = MODE_NORMAL;
static bool tcp_connected = false;

/* ATTENTION mode state */
static int  s_req_type       = 0;
static int  s_option_count   = 0;
static int  s_attn_focus     = 0;
static bool s_multi_selected[8];
static bool s_has_suggestions = false;

/* Option labels + descriptions */
static char s_command_text[128];
static char s_desc_text[128];
static char s_hint_text[128];
static char s_option_labels[8][32];
static char s_option_descs[8][64];
static int  s_opt_scroll = 0;
static char s_suggestions_text[256];

/* ----------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------- */
static void set_display_mode(buddy_display_mode_t mode);
static void update_attention_display(void);
static void submit_current_selection(void);
static int  get_visible_count(void);

/* ----------------------------------------------------------------
 * Custom draw callback for pet canvas
 * Dispatches to the current species' state function
 * ---------------------------------------------------------------- */
static void pet_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);

    buddy_info_t info = buddy_get_info();
    buddy_state_t state = info.state;

    int species_idx = buddy_get_species_index();
    if (species_idx < 0 || species_idx >= buddy_species_count) return;

    const buddy_species_desc_t *sp = buddy_species_table[species_idx];
    if (!sp || !sp->states[state]) return;

    buddy_render_begin(layer);
    sp->states[state](buddy_get_tick_count());
}

/* Half-size pet for attention mode's upper-right corner */
static void attn_pet_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *canvas = lv_event_get_target(e);

    buddy_info_t info = buddy_get_info();
    buddy_state_t state = info.state;

    int species_idx = buddy_get_species_index();
    if (species_idx < 0 || species_idx >= buddy_species_count) return;

    const buddy_species_desc_t *sp = buddy_species_table[species_idx];
    if (!sp || !sp->states[state]) return;

    lv_area_t coords;
    lv_obj_get_coords(canvas, &coords);
    int cx = (coords.x1 + coords.x2) / 2;
    int cy = coords.y1;
    int cw = coords.x2 - coords.x1 + 1;
    int ch = coords.y2 - coords.y1 + 1;

    buddy_render_begin_small(layer, cx, cy, cw, ch);
    sp->states[state](buddy_get_tick_count());
}

/* ----------------------------------------------------------------
 * Input callbacks
 * ---------------------------------------------------------------- */

static void buddy_on_encoder_cw(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    } else {
        s_attn_focus = (s_attn_focus + 1) % get_visible_count();
        update_attention_display();
    }
}

static void buddy_on_encoder_ccw(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_switch_screen(UI_SCREEN_POMODORO);
    } else {
        int visible = get_visible_count();
        s_attn_focus = (s_attn_focus - 1 + visible) % visible;
        update_attention_display();
    }
}

static void buddy_on_encoder_press(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_push_screen(UI_SCREEN_SETTINGS_BUDDY);
    }
}

static void buddy_on_encoder_long_press(void)
{
    if (display_mode != MODE_NORMAL) return;

    int count = buddy_get_species_count();
    if (count <= 1) return;

    int cur = buddy_get_species_index();
    int next;
    do {
        next = (int)(esp_random() % count);
    } while (next == cur);

    buddy_set_species(next);
}

static void buddy_on_settings_press(void)
{
    if (display_mode == MODE_NORMAL) {
        buddy_trigger_random();
    } else {
        submit_current_selection();
    }
}

/* ----------------------------------------------------------------
 * Display mode switching
 * ---------------------------------------------------------------- */

static void set_display_mode(buddy_display_mode_t mode)
{
    display_mode = mode;

    lvgl_lock();

    if (attn_container) {
        lv_obj_add_flag(attn_container, LV_OBJ_FLAG_HIDDEN);
    }

    bool show_normal = (mode == MODE_NORMAL);
    lv_obj_t *normal_objs[] = {conn_label, state_label, pet_canvas,
                               heart_label, stats_label, nav_hint};
    for (int i = 0; i < 6; i++) {
        if (normal_objs[i]) {
            if (show_normal) {
                lv_obj_clear_flag(normal_objs[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(normal_objs[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (mode == MODE_ATTENTION) {
        if (attn_container) {
            lv_obj_clear_flag(attn_container, LV_OBJ_FLAG_HIDDEN);
            s_attn_focus = 0;
            update_attention_display();
        }
    }

    lvgl_unlock();
}

/* ----------------------------------------------------------------
 * Helper: number of visible items in ATTENTION mode
 * ---------------------------------------------------------------- */
static int get_visible_count(void)
{
    if (s_req_type == REQ_PERMISSION) {
        return s_has_suggestions ? 3 : 2;
    } else if (s_req_type == REQ_MULTI_SELECT) {
        return s_option_count + 1;
    } else {
        return s_option_count;
    }
}

/* ----------------------------------------------------------------
 * Helper: get description text for currently focused item
 * ---------------------------------------------------------------- */
static const char *get_focused_desc(void)
{
    if (s_req_type == REQ_PERMISSION) {
        if (s_attn_focus == 0) {
            return s_desc_text[0] ? s_desc_text : (s_hint_text[0] ? s_hint_text : "");
        } else if (s_has_suggestions && s_attn_focus == 1) {
            return s_suggestions_text[0] ? s_suggestions_text : (s_desc_text[0] ? s_desc_text : "");
        } else {
            return "";
        }
    } else if (s_req_type == REQ_SINGLE_SELECT) {
        if (s_attn_focus < s_option_count && s_option_descs[s_attn_focus][0]) {
            return s_option_descs[s_attn_focus];
        }
        return s_desc_text[0] ? s_desc_text : "";
    } else { /* REQ_MULTI_SELECT */
        if (s_attn_focus == s_option_count) {
            return "";
        }
        if (s_attn_focus < s_option_count && s_option_descs[s_attn_focus][0]) {
            return s_option_descs[s_attn_focus];
        }
        return s_desc_text[0] ? s_desc_text : "";
    }
}

/* ----------------------------------------------------------------
 * Attention mode display
 * ---------------------------------------------------------------- */

static void update_attention_display(void)
{
    if (!attn_container) return;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

    lvgl_lock();

    int total = get_visible_count();

    /* Ensure focused item visible */
    if (s_attn_focus < s_opt_scroll) s_opt_scroll = s_attn_focus;
    if (s_attn_focus >= s_opt_scroll + OPT_VISIBLE) s_opt_scroll = s_attn_focus - OPT_VISIBLE + 1;
    if (s_opt_scroll < 0) s_opt_scroll = 0;
    int max_scroll = total - OPT_VISIBLE;
    if (max_scroll < 0) max_scroll = 0;
    if (s_opt_scroll > max_scroll) s_opt_scroll = max_scroll;

    /* Populate visible option slots */
    for (int i = 0; i < OPT_VISIBLE; i++) {
        if (!attn_opt_labels[i]) continue;
        int idx = s_opt_scroll + i;

        if (idx < total) {
            lv_obj_clear_flag(attn_opt_labels[i], LV_OBJ_FLAG_HIDDEN);
            char buf[64] = "";

            if (s_req_type == REQ_PERMISSION) {
                const char *text;
                if (idx == 0) text = i18n(STR_APPROVE);
                else if (s_has_suggestions && idx == 1) text = i18n(STR_APPROVE_REMEMBER);
                else text = i18n(STR_DENY);

                bool is_deny = (idx == total - 1);
                bool focused = (s_attn_focus == idx);
                snprintf(buf, sizeof(buf), "%s %s", focused ? ">" : " ", text);
                lv_label_set_text(attn_opt_labels[i], buf);
                lv_obj_set_style_text_color(attn_opt_labels[i],
                    lv_color_hex(focused ? (is_deny ? 0xFF4444 : 0x00FF00) : 0xAAAAAA), 0);
            } else if (s_req_type == REQ_SINGLE_SELECT) {
                bool focused = (s_attn_focus == idx);
                snprintf(buf, sizeof(buf), "%s %s", focused ? ">" : " ", s_option_labels[idx]);
                lv_label_set_text(attn_opt_labels[i], buf);
                lv_obj_set_style_text_color(attn_opt_labels[i],
                    lv_color_hex(focused ? 0x00FF00 : 0xCCCCCC), 0);
            } else { /* REQ_MULTI_SELECT */
                if (idx == s_option_count) {
                    bool focused = (s_attn_focus == idx);
                    snprintf(buf, sizeof(buf), "%s \xef\x9c\x93 %s", focused ? ">" : " ", i18n(STR_SUBMIT));
                    lv_label_set_text(attn_opt_labels[i], buf);
                    lv_obj_set_style_text_color(attn_opt_labels[i],
                        lv_color_hex(focused ? 0x00FF00 : 0xAAAAAA), 0);
                } else {
                    bool checked = s_multi_selected[idx];
                    bool focused = (s_attn_focus == idx);
                    snprintf(buf, sizeof(buf), "%s[%s] %s",
                             focused ? ">" : " ",
                             checked ? "\xef\x9c\x93" : " ",
                             s_option_labels[idx]);
                    lv_label_set_text(attn_opt_labels[i], buf);
                    lv_obj_set_style_text_color(attn_opt_labels[i],
                        lv_color_hex(focused ? 0x00FF00 : (checked ? 0x4D96FF : 0xCCCCCC)), 0);
                }
            }
        } else {
            lv_label_set_text(attn_opt_labels[i], "");
            lv_obj_add_flag(attn_opt_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Scroll indicator */
    if (attn_scrollbar && total > OPT_VISIBLE) {
        int track_h = OPT_VISIBLE * OPT_ROW_H;
        int bar_h = (OPT_VISIBLE * track_h) / total;
        if (bar_h < 10) bar_h = 10;
        int bar_y = (max_scroll > 0) ? (s_opt_scroll * (track_h - bar_h)) / max_scroll : 0;
        lv_obj_set_size(attn_scrollbar, 3, bar_h);
        lv_obj_set_pos(attn_scrollbar, 228, OPT_Y_START + bar_y);
        lv_obj_clear_flag(attn_scrollbar, LV_OBJ_FLAG_HIDDEN);
    } else if (attn_scrollbar) {
        lv_obj_add_flag(attn_scrollbar, LV_OBJ_FLAG_HIDDEN);
    }

    /* Invalidate attention pet canvas */
    if (attn_canvas) {
        lv_obj_invalidate(attn_canvas);
    }

    /* Bottom description */
    if (attn_desc) {
        lv_label_set_text(attn_desc, get_focused_desc());
    }

#pragma GCC diagnostic pop
    lvgl_unlock();
}

/* ----------------------------------------------------------------
 * Submit current selection
 * ---------------------------------------------------------------- */

static void submit_current_selection(void)
{
    if (s_req_type == REQ_PERMISSION) {
        int deny_idx = s_has_suggestions ? 2 : 1;
        if (s_attn_focus == deny_idx) {
            buddy_deny();
        } else {
            if (s_has_suggestions && s_attn_focus == 1) {
                buddy_include_rules(true);
            }
            buddy_approve();
        }
    } else if (s_req_type == REQ_SINGLE_SELECT) {
        const char *sel = s_option_labels[s_attn_focus];
        buddy_set_answer_labels(&sel, 1, false);
        buddy_submit_answer();
    } else { /* REQ_MULTI_SELECT */
        int submit_idx = s_option_count;
        if (s_attn_focus == submit_idx) {
            const char *checked[8];
            int count = 0;
            for (int i = 0; i < s_option_count; i++) {
                if (s_multi_selected[i]) {
                    checked[count++] = s_option_labels[i];
                }
            }
            if (count > 0) {
                buddy_set_answer_labels(checked, count, true);
                buddy_submit_answer();
            }
        } else {
            s_multi_selected[s_attn_focus] = !s_multi_selected[s_attn_focus];
            update_attention_display();
            return;
        }
    }

    set_display_mode(MODE_NORMAL);
}

/* ----------------------------------------------------------------
 * State color / text mapping
 * ---------------------------------------------------------------- */

static uint32_t state_to_color(buddy_state_t state)
{
    switch (state) {
    case BUDDY_SLEEP:     return 0x666666;
    case BUDDY_IDLE:      return 0x00FF88;
    case BUDDY_BUSY:      return 0x00BFFF;
    case BUDDY_ATTENTION: return 0xFF3333;
    case BUDDY_CELEBRATE: return 0xFFD700;
    case BUDDY_DIZZY:     return 0xCC66FF;
    case BUDDY_HEART:     return 0xFF69B4;
    default:              return 0xCCCCCC;
    }
}

static const char *state_to_text(buddy_state_t state)
{
    switch (state) {
    case BUDDY_SLEEP:     return i18n(STR_STATE_SLEEP);
    case BUDDY_IDLE:      return i18n(STR_STATE_IDLE);
    case BUDDY_BUSY:      return i18n(STR_STATE_BUSY);
    case BUDDY_ATTENTION: return i18n(STR_STATE_ATTENTION);
    case BUDDY_CELEBRATE: return i18n(STR_STATE_CELEBRATE);
    case BUDDY_DIZZY:     return i18n(STR_STATE_DIZZY);
    case BUDDY_HEART:     return i18n(STR_STATE_HEART);
    default:              return "?";
    }
}

/* ================================================================
 * Public API: screen creation
 * ================================================================ */

lv_obj_t* ui_screen_buddy_create(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    /* ---- Top bar: conn + session (left), state (right) ---- */
    conn_label = lv_label_create(screen);
    lv_obj_set_style_text_color(conn_label, lv_color_hex(0x444444), 0);
    lv_label_set_text(conn_label, "\xe2\x9c\x97");
    lv_obj_set_style_text_font(conn_label, &custom_font_14, 0);
    lv_obj_set_pos(conn_label, 8, 6);
    lv_obj_set_size(conn_label, 140, 20);
    lv_label_set_long_mode(conn_label, LV_LABEL_LONG_DOT);

    state_label = lv_label_create(screen);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x888888), 0);
    char init_buf[40];
    snprintf(init_buf, sizeof(init_buf), i18n(STR_STATE_SLEEP));
    lv_label_set_text(state_label, init_buf);
    lv_obj_set_style_text_font(state_label, &custom_font_14, 0);
    lv_obj_align(state_label, LV_ALIGN_TOP_RIGHT, -8, 6);

    /* ---- Pet canvas (custom drawing area) ---- */
    pet_canvas = lv_obj_create(screen);
    lv_obj_remove_style_all(pet_canvas);
    lv_obj_set_size(pet_canvas, 240, 120);
    lv_obj_align(pet_canvas, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_event_cb(pet_canvas, pet_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* ---- Heart meter (below pet) ---- */
    heart_label = lv_label_create(screen);
    lv_obj_set_style_text_color(heart_label, lv_color_hex(0xFF6B8A), 0);
    lv_label_set_text(heart_label, "\xe2\x99\xa5\xe2\x99\xa5\xe2\x99\xa5--");
    lv_obj_set_style_text_font(heart_label, &custom_font_14, 0);
    lv_obj_align(heart_label, LV_ALIGN_TOP_MID, 0, 160);

    /* ---- Stats (below heart, always visible) ---- */
    stats_label = lv_label_create(screen);
    lv_obj_set_style_text_color(stats_label, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_text(stats_label, "");
    lv_obj_set_style_text_font(stats_label, &custom_font_14, 0);
    lv_obj_align(stats_label, LV_ALIGN_TOP_MID, 0, 182);

    /* ---- Navigation hint (bottom center) ---- */
    nav_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(nav_hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(nav_hint, i18n(STR_H_BUDDY_HINT));
    lv_obj_set_style_text_font(nav_hint, &custom_font_14, 0);
    lv_obj_align(nav_hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* ============================================================
     * ATTENTION mode overlay (initially hidden)
     * ============================================================
     *  Layout (236×236 container, border 2px, content to y=232):
     *   y=4    ToolName(16px, w=110, h=22)  Pet(110×48) at x=120,y=4
     *   y=28   Command(14px, w=110, h=156,  Options(x=120, y=72, w=110)
     *          wrap)                          bottom-align at y=182
     *   y=184  Description (14px, 2-line, h=36)
     */
    attn_container = lv_obj_create(screen);
    lv_obj_set_size(attn_container, 236, 236);
    lv_obj_align(attn_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(attn_container, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(attn_container, 2, 0);
    lv_obj_set_style_border_color(attn_container, lv_color_hex(0xFF4444), 0);
    lv_obj_add_flag(attn_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(attn_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(attn_container, 0, 0);

    /* Tool name — 16px, h=22 */
    attn_tool = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_tool, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(attn_tool, "");
    lv_obj_set_style_text_font(attn_tool, &custom_font_16, 0);
    lv_obj_set_pos(attn_tool, 6, 4);
    lv_obj_set_size(attn_tool, 110, 22);
    lv_label_set_long_mode(attn_tool, LV_LABEL_LONG_DOT);

    /* Command text — 14px, auto-wrap, left column */
    attn_cmd = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_cmd, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_text(attn_cmd, "");
    lv_obj_set_style_text_font(attn_cmd, &custom_font_14, 0);
    lv_obj_set_pos(attn_cmd, 6, 28);
    lv_obj_set_size(attn_cmd, 110, 156);
    lv_label_set_long_mode(attn_cmd, LV_LABEL_LONG_WRAP);

    /* Right: half-size pet canvas */
    attn_canvas = lv_obj_create(attn_container);
    lv_obj_remove_style_all(attn_canvas);
    lv_obj_set_size(attn_canvas, 110, 48);
    lv_obj_set_pos(attn_canvas, 120, 4);
    lv_obj_add_event_cb(attn_canvas, attn_pet_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* Options — right side, bottom-aligned with command area */
    for (int i = 0; i < OPT_VISIBLE; i++) {
        attn_opt_labels[i] = lv_label_create(attn_container);
        lv_obj_set_style_text_font(attn_opt_labels[i], &custom_font_14, 0);
        lv_obj_set_style_text_color(attn_opt_labels[i], lv_color_hex(0xCCCCCC), 0);
        lv_label_set_text(attn_opt_labels[i], "");
        lv_obj_set_pos(attn_opt_labels[i], 120, OPT_Y_START + i * OPT_ROW_H);
        lv_obj_set_size(attn_opt_labels[i], 110, OPT_ROW_H);
        lv_label_set_long_mode(attn_opt_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_add_flag(attn_opt_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Scrollbar for options */
    attn_scrollbar = lv_obj_create(attn_container);
    lv_obj_remove_style_all(attn_scrollbar);
    lv_obj_set_style_bg_color(attn_scrollbar, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(attn_scrollbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(attn_scrollbar, 1, 0);
    lv_obj_add_flag(attn_scrollbar, LV_OBJ_FLAG_HIDDEN);

    /* Bottom: description — 14px, 2-line wrap */
    attn_desc = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_desc, lv_color_hex(0xFFCC00), 0);
    lv_label_set_text(attn_desc, "");
    lv_obj_set_style_text_font(attn_desc, &custom_font_14, 0);
    lv_obj_set_pos(attn_desc, 6, 184);
    lv_obj_set_size(attn_desc, 224, 36);
    lv_label_set_long_mode(attn_desc, LV_LABEL_LONG_WRAP);

    /* ---- Register input callbacks ---- */
    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw         = buddy_on_encoder_cw,
        .on_encoder_ccw        = buddy_on_encoder_ccw,
        .on_encoder_press      = buddy_on_encoder_press,
        .on_encoder_long_press = buddy_on_encoder_long_press,
        .on_settings_press     = buddy_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_BUDDY, &cbs);

    ESP_LOGI(TAG, "Buddy screen created");
    return screen;
}

/* ================================================================
 * Public API: update state (called periodically)
 * ================================================================ */

void ui_screen_buddy_update_state(void)
{
    buddy_info_t info = buddy_get_info();
    uint32_t color = state_to_color(info.state);
    const char *state_text = state_to_text(info.state);

    lvgl_lock();

    /* Invalidate pet canvases to trigger custom draw */
    if (pet_canvas && display_mode == MODE_NORMAL) {
        lv_obj_invalidate(pet_canvas);
    }
    if (attn_canvas && display_mode == MODE_ATTENTION) {
        lv_obj_invalidate(attn_canvas);
    }

    /* State label (top-right, just the state text) */
    if (state_label) {
        lv_label_set_text(state_label, state_text);
        lv_obj_set_style_text_color(state_label, lv_color_hex(color), 0);
    }

    /* Refresh i18n labels */
    if (nav_hint)   lv_label_set_text(nav_hint, i18n(STR_H_BUDDY_HINT));

    /* Connection label: ✓project_name or ✗ */
    if (conn_label) {
        if (info.tcp_connected) {
            const char *project = tcp_service_get_project();
            char session[9] = {0};
            tcp_service_load_pairing_code(session, sizeof(session));
            const char *name = project ? project : (session[0] ? session : NULL);
            if (name) {
                char buf[40];
                snprintf(buf, sizeof(buf), "\xe2\x9c\x93%s", name);
                lv_obj_set_style_text_color(conn_label, lv_color_hex(0x00FF88), 0);
                lv_label_set_text(conn_label, buf);
            } else {
                lv_obj_set_style_text_color(conn_label, lv_color_hex(0x00FF88), 0);
                lv_label_set_text(conn_label, "\xe2\x9c\x93");
            }
        } else {
            lv_obj_set_style_text_color(conn_label, lv_color_hex(0x444444), 0);
            lv_label_set_text(conn_label, "\xe2\x9c\x97");
        }
    }

    /* Heart meter */
    if (heart_label) {
        char buf[16];
        int off = 0;
        for (int i = 0; i < info.heart_level && i < 5; i++) {
            off += snprintf(buf + off, sizeof(buf) - off, "\xe2\x99\xa5");
        }
        for (int i = info.heart_level; i < 5; i++) {
            off += snprintf(buf + off, sizeof(buf) - off, "-");
        }
        lv_label_set_text(heart_label, buf);
    }

    /* Stats — always visible, show total counts */
    if (stats_label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "\xe2\x9c\x85%lu  \xe2\x9d\x8c%lu",
                 (unsigned long)info.approved_count,
                 (unsigned long)info.denied_count);
        lv_label_set_text(stats_label, buf);
    }

    lvgl_unlock();
}

/* ================================================================
 * Public API: show request (ATTENTION mode)
 * ================================================================ */

void ui_screen_buddy_show_request(const char *tool, const char *command,
                                   const char *description, const char *hint,
                                   int option_count, int req_type,
                                   const char *option_labels[], int option_count_labels,
                                   const char *option_descs[],
                                   bool has_suggestions, const char *suggestions_text)
{
    if (option_count < 0) option_count = 0;
    if (option_count_labels < 0) option_count_labels = 0;
    if (req_type < 0 || req_type > 2) req_type = REQ_PERMISSION;

    s_req_type = req_type;
    s_option_count = (option_count > 8) ? 8 : option_count;
    s_attn_focus = 0;
    s_has_suggestions = has_suggestions;
    memset(s_multi_selected, 0, sizeof(s_multi_selected));
    memset(s_option_labels, 0, sizeof(s_option_labels));
    memset(s_option_descs, 0, sizeof(s_option_descs));
    memset(s_hint_text, 0, sizeof(s_hint_text));
    memset(s_command_text, 0, sizeof(s_command_text));
    memset(s_desc_text, 0, sizeof(s_desc_text));
    memset(s_suggestions_text, 0, sizeof(s_suggestions_text));

    /* Store command (actual operation) */
    if (command && command[0]) {
        snprintf(s_command_text, sizeof(s_command_text), "%s", command);
    }
    /* Store description */
    if (description && description[0]) {
        snprintf(s_desc_text, sizeof(s_desc_text), "%s", description);
    }
    /* Store hint as fallback */
    if (hint && hint[0]) {
        snprintf(s_hint_text, sizeof(s_hint_text), "%s", hint);
    }
    /* Store suggestions text for "Approve and Remember" */
    if (suggestions_text && suggestions_text[0]) {
        snprintf(s_suggestions_text, sizeof(s_suggestions_text), "%s", suggestions_text);
    }

    /* Copy option labels and descriptions */
    int labels_to_copy = (option_count_labels < s_option_count) ? option_count_labels : s_option_count;
    for (int i = 0; i < labels_to_copy; i++) {
        if (option_labels && option_labels[i]) {
            snprintf(s_option_labels[i], sizeof(s_option_labels[i]), "%s", option_labels[i]);
        } else {
            snprintf(s_option_labels[i], sizeof(s_option_labels[i]), "?");
        }
        if (option_descs && option_descs[i]) {
            snprintf(s_option_descs[i], sizeof(s_option_descs[i]), "%s", option_descs[i]);
        }
    }

    lvgl_lock();

    /* Tool name — just the tool name, no prefix */
    if (attn_tool) {
        if (tool && tool[0]) {
            lv_label_set_text(attn_tool, tool);
        } else {
            lv_label_set_text(attn_tool, req_type == REQ_PERMISSION ? "Permission" : "Question");
        }
    }

    /* Command area — show the actual command/operation text */
    if (attn_cmd) {
        lv_label_set_text(attn_cmd, s_command_text[0] ? s_command_text :
                          (s_hint_text[0] ? s_hint_text : ""));
    }

    /* Reset options scroll */
    s_opt_scroll = 0;

    /* Update attention display */
    update_attention_display();

    lvgl_unlock();

    set_display_mode(MODE_ATTENTION);
}

/* ================================================================
 * Public API: clear request
 * ================================================================ */

void ui_screen_buddy_clear_request(void)
{
    set_display_mode(MODE_NORMAL);
}

/* ================================================================
 * Public API: update TCP connection status
 * ================================================================ */

void ui_screen_buddy_set_connected(bool connected)
{
    tcp_connected = connected;

    lvgl_lock();
    if (conn_label) {
        if (connected) {
            const char *project = tcp_service_get_project();
            char session[9] = {0};
            tcp_service_load_pairing_code(session, sizeof(session));
            const char *name = project ? project : (session[0] ? session : NULL);
            if (name) {
                char buf[40];
                snprintf(buf, sizeof(buf), "\xe2\x9c\x93%s", name);
                lv_obj_set_style_text_color(conn_label, lv_color_hex(0x00FF88), 0);
                lv_label_set_text(conn_label, buf);
            } else {
                lv_obj_set_style_text_color(conn_label, lv_color_hex(0x00FF88), 0);
                lv_label_set_text(conn_label, "\xe2\x9c\x93");
            }
        } else {
            lv_obj_set_style_text_color(conn_label, lv_color_hex(0x444444), 0);
            lv_label_set_text(conn_label, "\xe2\x9c\x97");
        }
    }
    lvgl_unlock();
}
