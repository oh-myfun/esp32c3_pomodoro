#include "ui_screen_buddy.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "buddy/buddy.h"
#include "esp_log.h"
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
static lv_obj_t *conn_icon     = NULL;
static lv_obj_t *name_label    = NULL;

/* Center — ASCII pet */
static lv_obj_t *pet_label     = NULL;

/* Status area */
static lv_obj_t *state_label   = NULL;
static lv_obj_t *msg_label     = NULL;

/* Bottom hint */
static lv_obj_t *nav_hint      = NULL;

/* ----------------------------------------------------------------
 * Static LVGL objects — ATTENTION mode overlay
 * ---------------------------------------------------------------- */
static lv_obj_t *attn_container = NULL;
static lv_obj_t *attn_title    = NULL;
static lv_obj_t *attn_pet      = NULL;
static lv_obj_t *attn_tool     = NULL;
static lv_obj_t *attn_hint     = NULL;
static lv_obj_t *attn_options[9] = {NULL}; /* up to 8 options + 1 submit */

/* ----------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------- */
static buddy_display_mode_t display_mode = MODE_NORMAL;
static bool tcp_connected = false;

/* ATTENTION mode state */
static int  s_req_type       = 0;       /* 0=permission, 1=single, 2=multi */
static int  s_option_count   = 0;       /* number of options */
static int  s_attn_focus     = 0;       /* currently focused option */
static bool s_multi_selected[8];        /* checkbox state for multi-select */

/* Option labels storage (max 8 options) */
static char s_option_labels[8][32];

/* ----------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------- */
static void set_display_mode(buddy_display_mode_t mode);
static void update_pet_animation(void);
static void update_attn_pet(void);
static void update_attention_display(void);
static void submit_current_selection(void);

/* ----------------------------------------------------------------
 * Input callbacks
 * ---------------------------------------------------------------- */

static void buddy_on_encoder_cw(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    } else { /* MODE_ATTENTION */
        int visible = (s_req_type == REQ_PERMISSION) ? 2 :
                      (s_req_type == REQ_MULTI_SELECT) ? (s_option_count + 1) : s_option_count;
        s_attn_focus = (s_attn_focus + 1) % visible;
        update_attention_display();
    }
}

static void buddy_on_encoder_ccw(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_switch_screen(UI_SCREEN_POMODORO);
    } else { /* MODE_ATTENTION */
        int visible = (s_req_type == REQ_PERMISSION) ? 2 :
                      (s_req_type == REQ_MULTI_SELECT) ? (s_option_count + 1) : s_option_count;
        s_attn_focus = (s_attn_focus - 1 + visible) % visible;
        update_attention_display();
    }
}

static void buddy_on_encoder_press(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_switch_screen(UI_SCREEN_SETTINGS_BRIDGE);
    }
    /* MODE_ATTENTION: no action on encoder press */
}

static void buddy_on_encoder_long_press(void)
{
    /* unused — long press no longer triggers dizzy */
}

static void buddy_on_settings_press(void)
{
    if (display_mode == MODE_NORMAL) {
        buddy_state_t state = buddy_get_info().state;
        if (state == BUDDY_IDLE || state == BUDDY_SLEEP) {
            buddy_trigger_random();
        }
    } else { /* MODE_ATTENTION */
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

    /* Hide / show overlays */
    if (attn_container) {
        lv_obj_add_flag(attn_container, LV_OBJ_FLAG_HIDDEN);
    }

    /* Show / hide normal-mode elements */
    bool show_normal = (mode == MODE_NORMAL);
    lv_obj_t *normal_objs[] = {conn_icon, name_label, state_label, msg_label, nav_hint};
    for (int i = 0; i < 5; i++) {
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
 * Pet animation helpers
 * ---------------------------------------------------------------- */

static void build_frame_text(char *buf, size_t buf_size)
{
    const char *const *frame = buddy_get_current_frame();
    if (!frame) {
        buf[0] = '\0';
        return;
    }
    int offset = 0;
    for (int i = 0; i < BUDDY_FRAME_LINES; i++) {
        offset += snprintf(buf + offset, buf_size - offset,
                           "%s\n", frame[i] ? frame[i] : "");
    }
    /* Remove trailing newline */
    if (offset > 0 && buf[offset - 1] == '\n') {
        buf[offset - 1] = '\0';
    }
}

static void update_pet_animation(void)
{
    if (!pet_label) return;
    char buf[BUDDY_FRAME_LINES * 16];
    build_frame_text(buf, sizeof(buf));
    lv_label_set_text(pet_label, buf);
}

static void update_attn_pet(void)
{
    if (!attn_pet) return;
    char buf[BUDDY_FRAME_LINES * 16];
    build_frame_text(buf, sizeof(buf));
    lv_label_set_text(attn_pet, buf);
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

    /* Hide all option labels first */
    int max_labels = (s_req_type == REQ_PERMISSION) ? 2 :
                     (s_req_type == REQ_MULTI_SELECT) ? (s_option_count + 1) : s_option_count;
    for (int i = 0; i < 9; i++) {
        if (attn_options[i]) {
            if (i < max_labels) {
                lv_obj_clear_flag(attn_options[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(attn_options[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (s_req_type == REQ_PERMISSION) {
        /* Permission: [0] = Allow, [1] = Deny */
        for (int i = 0; i < 2; i++) {
            if (!attn_options[i]) continue;
            char buf[48];
            const char *text = (i == 0) ? i18n(STR_APPROVE) : i18n(STR_DENY);
            bool focused = (s_attn_focus == i);
            snprintf(buf, sizeof(buf), "%s %s", focused ? ">" : " ", text);
            lv_label_set_text(attn_options[i], buf);
            if (focused) {
                lv_obj_set_style_text_color(attn_options[i],
                    lv_color_hex(i == 0 ? 0x00FF00 : 0xFF4444), 0);
            } else {
                lv_obj_set_style_text_color(attn_options[i],
                    lv_color_hex(0xAAAAAA), 0);
            }
        }
    } else if (s_req_type == REQ_SINGLE_SELECT) {
        /* Single select: each option, focused one has > prefix */
        for (int i = 0; i < s_option_count; i++) {
            if (!attn_options[i]) continue;
            char buf[64];
            bool focused = (s_attn_focus == i);
            snprintf(buf, sizeof(buf), "%s %s", focused ? ">" : " ", s_option_labels[i]);
            lv_label_set_text(attn_options[i], buf);
            lv_obj_set_style_text_color(attn_options[i],
                lv_color_hex(focused ? 0x00FF00 : 0xCCCCCC), 0);
        }
    } else { /* REQ_MULTI_SELECT */
        /* Multi select: checkboxes + submit button at end */
        for (int i = 0; i < s_option_count; i++) {
            if (!attn_options[i]) continue;
            char buf[64];
            bool checked = s_multi_selected[i];
            bool focused = (s_attn_focus == i);
            snprintf(buf, sizeof(buf), "%s[%s] %s",
                     focused ? ">" : " ",
                     checked ? "\xef\x9c\x93" : " ",
                     s_option_labels[i]);
            lv_label_set_text(attn_options[i], buf);
            lv_obj_set_style_text_color(attn_options[i],
                lv_color_hex(focused ? 0x00FF00 : (checked ? 0x4D96FF : 0xCCCCCC)), 0);
        }
        /* Submit button */
        int submit_idx = s_option_count;
        if (attn_options[submit_idx]) {
            char buf[64];
            bool focused = (s_attn_focus == submit_idx);
            snprintf(buf, sizeof(buf), "%s \xef\x9c\x93 Submit", focused ? ">" : " ");
            lv_label_set_text(attn_options[submit_idx], buf);
            lv_obj_set_style_text_color(attn_options[submit_idx],
                lv_color_hex(focused ? 0x00FF00 : 0xAAAAAA), 0);
        }
    }

    /* Update pet in attention mode */
    update_attn_pet();

#pragma GCC diagnostic pop
    lvgl_unlock();
}

/* ----------------------------------------------------------------
 * Submit current selection
 * ---------------------------------------------------------------- */

static void submit_current_selection(void)
{
    if (s_req_type == REQ_PERMISSION) {
        if (s_attn_focus == 0) {
            buddy_approve();
        } else {
            buddy_deny();
        }
    } else if (s_req_type == REQ_SINGLE_SELECT) {
        /* Delegate to buddy_submit_answer */
        buddy_submit_answer();
    } else { /* REQ_MULTI_SELECT */
        int submit_idx = s_option_count;
        if (s_attn_focus == submit_idx) {
            /* Submit only if at least 1 selected */
            int count = 0;
            for (int i = 0; i < s_option_count; i++) {
                if (s_multi_selected[i]) count++;
            }
            if (count > 0) {
                buddy_submit_answer();
            }
        } else {
            /* Toggle checkbox for the focused regular option */
            s_multi_selected[s_attn_focus] = !s_multi_selected[s_attn_focus];
            update_attention_display();
            return; /* don't switch back to normal */
        }
    }

    /* Return to normal mode after submit */
    set_display_mode(MODE_NORMAL);
}

/* ----------------------------------------------------------------
 * State color mapping
 * ---------------------------------------------------------------- */

static uint32_t state_to_color(buddy_state_t state)
{
    switch (state) {
    case BUDDY_SLEEP:     return 0x888888;
    case BUDDY_IDLE:      return 0x00FF00;
    case BUDDY_BUSY:      return 0x4D96FF;
    case BUDDY_ATTENTION: return 0xFF4444;
    case BUDDY_CELEBRATE: return 0xFFFF00;
    case BUDDY_DIZZY:     return 0xFF00FF;
    case BUDDY_HEART:     return 0xFF6B8A;
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

    /* ---- Top bar ---- */
    conn_icon = lv_label_create(screen);
    lv_obj_set_style_text_color(conn_icon, lv_color_hex(0x666666), 0);
    lv_label_set_text(conn_icon, "\xe2\x9c\x97");
    lv_obj_set_style_text_font(conn_icon, &custom_font_14, 0);
    lv_obj_align(conn_icon, LV_ALIGN_TOP_LEFT, 8, 6);

    name_label = lv_label_create(screen);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(name_label, i18n(STR_BUDDY_NAME));
    lv_obj_set_style_text_font(name_label, &custom_font_14, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 35, 6);

    /* ---- Pet animation (center) ---- */
    pet_label = lv_label_create(screen);
    lv_obj_set_style_text_color(pet_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(pet_label, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_line_space(pet_label, 0, 0);
    lv_obj_align(pet_label, LV_ALIGN_CENTER, -40, -20);

    /* ---- State label ---- */
    state_label = lv_label_create(screen);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x00FF00), 0);
    char init_state_buf[40];
    snprintf(init_state_buf, sizeof(init_state_buf), i18n(STR_FMT_STATE), i18n(STR_STATE_SLEEP));
    lv_label_set_text(state_label, init_state_buf);
    lv_obj_set_style_text_font(state_label, &custom_font_14, 0);
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_LEFT, 8, -40);

    /* ---- Message label ---- */
    msg_label = lv_label_create(screen);
    lv_obj_set_style_text_color(msg_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(msg_label, "");
    lv_obj_set_style_text_font(msg_label, &custom_font_14, 0);
    lv_obj_align(msg_label, LV_ALIGN_BOTTOM_LEFT, 8, -24);
    lv_obj_set_width(msg_label, 224);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_MODE_WRAP);

    /* ---- Navigation hint ---- */
    nav_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(nav_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(nav_hint, i18n(STR_H_PRESS_BACK_SET_INFO));
    lv_obj_set_style_text_font(nav_hint, &custom_font_14, 0);
    lv_obj_align(nav_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* ============================================================
     * ATTENTION mode overlay (initially hidden)
     * ============================================================ */
    attn_container = lv_obj_create(screen);
    lv_obj_set_size(attn_container, 236, 236);
    lv_obj_align(attn_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(attn_container, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(attn_container, 2, 0);
    lv_obj_set_style_border_color(attn_container, lv_color_hex(0xFF4444), 0);
    lv_obj_add_flag(attn_container, LV_OBJ_FLAG_HIDDEN);

    attn_title = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_title, lv_color_hex(0xFF4444), 0);
    lv_label_set_text(attn_title, i18n(STR_PERMISSION));
    lv_obj_set_style_text_font(attn_title, &custom_font_16, 0);
    lv_obj_align(attn_title, LV_ALIGN_TOP_MID, 0, 8);

    /* Pet animation in attention mode (small, top-left) */
    attn_pet = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_pet, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(attn_pet, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_line_space(attn_pet, 0, 0);
    lv_obj_align(attn_pet, LV_ALIGN_TOP_LEFT, 10, 28);

    /* Tool name label */
    attn_tool = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_tool, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(attn_tool, i18n(STR_TOOL));
    lv_obj_set_style_text_font(attn_tool, &custom_font_14, 0);
    lv_obj_align(attn_tool, LV_ALIGN_TOP_LEFT, 10, 78);

    /* Hint / description label */
    attn_hint = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_hint, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_text(attn_hint, "");
    lv_obj_set_style_text_font(attn_hint, &custom_font_14, 0);
    lv_obj_align(attn_hint, LV_ALIGN_TOP_LEFT, 10, 96);
    lv_obj_set_width(attn_hint, 216);
    lv_label_set_long_mode(attn_hint, LV_LABEL_LONG_MODE_WRAP);

    /* Option labels — up to 9 (8 options + 1 submit for multi-select) */
    for (int i = 0; i < 9; i++) {
        attn_options[i] = lv_label_create(attn_container);
        lv_obj_set_style_text_font(attn_options[i], &custom_font_14, 0);
        lv_obj_set_style_text_color(attn_options[i], lv_color_hex(0xCCCCCC), 0);
        lv_label_set_text(attn_options[i], "");
        /* Position below hint area: base_y=120, each line 16px apart */
        lv_obj_align(attn_options[i], LV_ALIGN_TOP_LEFT, 16, 120 + i * 16);
        lv_obj_add_flag(attn_options[i], LV_OBJ_FLAG_HIDDEN);
    }

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

    /* Update pet animation */
    update_pet_animation();
    if (display_mode == MODE_ATTENTION) {
        update_attn_pet();
    }

    /* State label */
    if (state_label) {
        char buf[40];
        snprintf(buf, sizeof(buf), i18n(STR_FMT_STATE), state_text);
        lv_label_set_text(state_label, buf);
        lv_obj_set_style_text_color(state_label, lv_color_hex(color), 0);
    }

    /* Refresh static i18n labels */
    if (name_label) lv_label_set_text(name_label, i18n(STR_BUDDY_NAME));
    if (nav_hint)   lv_label_set_text(nav_hint, i18n(STR_H_PRESS_BACK_SET_INFO));
    if (attn_title) lv_label_set_text(attn_title, i18n(STR_PERMISSION));

    /* Pet label color */
    if (pet_label) {
        lv_obj_set_style_text_color(pet_label, lv_color_hex(color), 0);
    }

    lvgl_unlock();
}

/* ================================================================
 * Public API: show request (ATTENTION mode)
 * ================================================================ */

void ui_screen_buddy_show_request(const char *tool, const char *hint,
                                   int option_count, int req_type)
{
    s_req_type = req_type;
    s_option_count = (option_count > 8) ? 8 : option_count;
    s_attn_focus = 0;
    memset(s_multi_selected, 0, sizeof(s_multi_selected));
    memset(s_option_labels, 0, sizeof(s_option_labels));

    lvgl_lock();

    /* Update title based on request type */
    if (attn_title) {
        if (req_type == REQ_PERMISSION) {
            lv_label_set_text(attn_title, i18n(STR_PERMISSION));
        } else {
            /* Single/Multi select — show question or "Question" */
            lv_label_set_text(attn_title, hint ? hint : "Question");
        }
    }

    /* Tool label */
    if (attn_tool) {
        if (req_type == REQ_PERMISSION && tool) {
            char buf[48];
            snprintf(buf, sizeof(buf), i18n(STR_FMT_TOOL), tool);
            lv_label_set_text(attn_tool, buf);
        } else {
            lv_label_set_text(attn_tool, "");
        }
    }

    /* Hint label */
    if (attn_hint) {
        lv_label_set_text(attn_hint, hint ? hint : "");
    }

    /* For permission: set default option labels */
    if (req_type == REQ_PERMISSION) {
        /* Option labels will be set in update_attention_display */
    }

    /* Update attention display to show options */
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
    if (conn_icon) {
        if (connected) {
            lv_obj_set_style_text_color(conn_icon, lv_color_hex(0x00FF00), 0);
            lv_label_set_text(conn_icon, "\xe2\x9c\x93");
        } else {
            lv_obj_set_style_text_color(conn_icon, lv_color_hex(0x666666), 0);
            lv_label_set_text(conn_icon, "\xe2\x9c\x97");
        }
    }
    lvgl_unlock();
}
