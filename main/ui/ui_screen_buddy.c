#include "ui_screen_buddy.h"
#include "ui_manager.h"
#include "buddy/buddy.h"
#include "service/ble_service.h"
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
    MODE_INFO,
} buddy_display_mode_t;

/* ----------------------------------------------------------------
 * Static LVGL objects — Normal mode
 * ---------------------------------------------------------------- */
static lv_obj_t *screen        = NULL;

/* Top bar */
static lv_obj_t *ble_icon      = NULL;
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
static lv_obj_t *attn_title    = NULL;
static lv_obj_t *attn_pet      = NULL;
static lv_obj_t *attn_tool     = NULL;
static lv_obj_t *attn_hint     = NULL;
static lv_obj_t *attn_approve  = NULL;
static lv_obj_t *attn_deny     = NULL;
static lv_obj_t *attn_container = NULL;

/* ----------------------------------------------------------------
 * Static LVGL objects — INFO mode overlay
 * ---------------------------------------------------------------- */
static lv_obj_t *info_container = NULL;
static lv_obj_t *info_title     = NULL;
static lv_obj_t *info_labels[5] = {NULL};
static lv_obj_t *info_action    = NULL;
static lv_obj_t *info_hint      = NULL;

/* ----------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------- */
static buddy_display_mode_t display_mode = MODE_NORMAL;
static bool ble_connected = false;
static int  attn_selection = 0;  /* 0 = Approve, 1 = Deny */
static int  info_scroll   = 0;  /* 0 = Next Pet, 1 = (reserved) */

/* ----------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------- */
static void set_display_mode(buddy_display_mode_t mode);
static void update_pet_animation(void);
static void update_attention_selection(void);
static void update_info_content(void);

/* ----------------------------------------------------------------
 * Input callbacks
 * ---------------------------------------------------------------- */

static void buddy_on_encoder_cw(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    } else if (display_mode == MODE_ATTENTION) {
        attn_selection = 1;
        update_attention_selection();
    } else if (display_mode == MODE_INFO) {
        info_scroll = (info_scroll + 1) % 2;
        update_info_content();
    }
}

static void buddy_on_encoder_ccw(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_switch_screen(UI_SCREEN_POMODORO);
    } else if (display_mode == MODE_ATTENTION) {
        attn_selection = 0;
        update_attention_selection();
    } else if (display_mode == MODE_INFO) {
        info_scroll = (info_scroll - 1 + 2) % 2;
        update_info_content();
    }
}

static void buddy_on_encoder_press(void)
{
    if (display_mode == MODE_NORMAL) {
        ui_switch_screen(UI_SCREEN_POMODORO);
    } else if (display_mode == MODE_ATTENTION) {
        set_display_mode(MODE_NORMAL);
    } else if (display_mode == MODE_INFO) {
        set_display_mode(MODE_NORMAL);
    }
}

static void buddy_on_encoder_long_press(void)
{
    buddy_trigger_dizzy();
}

static void buddy_on_settings_press(void)
{
    if (display_mode == MODE_NORMAL) {
        set_display_mode(MODE_INFO);
    } else if (display_mode == MODE_ATTENTION) {
        if (attn_selection == 0) {
            buddy_approve();
        } else {
            buddy_deny();
        }
        set_display_mode(MODE_NORMAL);
    } else if (display_mode == MODE_INFO) {
        if (info_scroll == 0) {
            /* Next Pet */
            int count = buddy_get_species_count();
            buddy_info_t info = buddy_get_info();
            int next = (info.species_index + 1) % count;
            buddy_set_species(next);
            update_info_content();
        } else {
            /* Back */
            set_display_mode(MODE_NORMAL);
        }
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
    if (info_container) {
        lv_obj_add_flag(info_container, LV_OBJ_FLAG_HIDDEN);
    }

    /* Show normal-mode elements */
    bool show_normal = (mode == MODE_NORMAL);
    lv_obj_t *normal_objs[] = {ble_icon, name_label, state_label, msg_label, nav_hint};
    for (int i = 0; i < 5; i++) {
        if (normal_objs[i]) {
            if (show_normal) {
                lv_obj_clear_flag(normal_objs[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(normal_objs[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    switch (mode) {
    case MODE_ATTENTION:
        if (attn_container) {
            lv_obj_clear_flag(attn_container, LV_OBJ_FLAG_HIDDEN);
            attn_selection = 0;
            update_attention_selection();
        }
        break;
    case MODE_INFO:
        if (info_container) {
            lv_obj_clear_flag(info_container, LV_OBJ_FLAG_HIDDEN);
            info_scroll = 0;
            update_info_content();
        }
        break;
    default:
        break;
    }

    lvgl_unlock();
}

/* ----------------------------------------------------------------
 * Pet animation helper
 * ---------------------------------------------------------------- */

static void update_pet_animation(void)
{
    const char *const *frame = buddy_get_current_frame();
    if (!frame || !pet_label) return;

    /* Build a single string with \n separators for the 12 lines */
    char buf[BUDDY_FRAME_LINES * 20];
    int offset = 0;
    for (int i = 0; i < BUDDY_FRAME_LINES; i++) {
        int len = snprintf(buf + offset, sizeof(buf) - offset,
                          "%s\n", frame[i] ? frame[i] : "");
        if (len > 0) offset += len;
    }
    /* Remove trailing newline */
    if (offset > 0 && buf[offset - 1] == '\n') {
        buf[offset - 1] = '\0';
    }
    lv_label_set_text(pet_label, buf);
}

/* Also update the attention-mode pet */
static void update_attn_pet(void)
{
    const char *const *frame = buddy_get_current_frame();
    if (!frame || !attn_pet) return;

    char buf[BUDDY_FRAME_LINES * 20];
    int offset = 0;
    for (int i = 0; i < BUDDY_FRAME_LINES; i++) {
        int len = snprintf(buf + offset, sizeof(buf) - offset,
                          "%s\n", frame[i] ? frame[i] : "");
        if (len > 0) offset += len;
    }
    if (offset > 0 && buf[offset - 1] == '\n') {
        buf[offset - 1] = '\0';
    }
    lv_label_set_text(attn_pet, buf);
}

/* ----------------------------------------------------------------
 * Attention mode selection highlight
 * ---------------------------------------------------------------- */

static void update_attention_selection(void)
{
    if (!attn_approve || !attn_deny) return;

    lvgl_lock();
    if (attn_selection == 0) {
        lv_label_set_text(attn_approve, "> Approve");
        lv_label_set_text(attn_deny,    "  Deny");
        lv_obj_set_style_text_color(attn_approve, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_color(attn_deny,    lv_color_hex(0xFFFFFF), 0);
    } else {
        lv_label_set_text(attn_approve, "  Approve");
        lv_label_set_text(attn_deny,    "> Deny");
        lv_obj_set_style_text_color(attn_approve, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(attn_deny,    lv_color_hex(0xFF4444), 0);
    }
    lvgl_unlock();
}

/* ----------------------------------------------------------------
 * Info page content
 * ---------------------------------------------------------------- */

static void update_info_content(void)
{
    if (!info_container) return;

    buddy_info_t info = buddy_get_info();

    lvgl_lock();

    if (info_labels[0]) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Pet: %s",
                 buddy_get_species_name(info.species_index) ?
                 buddy_get_species_name(info.species_index) : "--");
        lv_label_set_text(info_labels[0], buf);
    }
    if (info_labels[1]) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Owner: %s",
                 info.owner_name[0] ? info.owner_name : "--");
        lv_label_set_text(info_labels[1], buf);
    }
    if (info_labels[2]) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Approved: %lu", (unsigned long)info.approved_count);
        lv_label_set_text(info_labels[2], buf);
    }
    if (info_labels[3]) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Denied: %lu", (unsigned long)info.denied_count);
        lv_label_set_text(info_labels[3], buf);
    }
    if (info_labels[4]) {
        lv_label_set_text(info_labels[4],
                          ble_connected ? "BLE: Connected" : "BLE: Disconnected");
    }

    if (info_action) {
        if (info_scroll == 0) {
            lv_label_set_text(info_action, "> Next Pet");
        } else {
            lv_label_set_text(info_action, "  Back");
        }
    }

    lvgl_unlock();
}

/* ----------------------------------------------------------------
 * State color mapping
 * ---------------------------------------------------------------- */

static uint32_t state_to_color(buddy_state_t state)
{
    switch (state) {
    case BUDDY_SLEEP:     return 0x888888;  /* gray */
    case BUDDY_IDLE:      return 0x00FF00;  /* green */
    case BUDDY_BUSY:      return 0x4D96FF;  /* blue */
    case BUDDY_ATTENTION: return 0xFF4444;  /* red */
    case BUDDY_CELEBRATE: return 0xFFFF00;  /* yellow */
    case BUDDY_DIZZY:     return 0xFF00FF;  /* magenta */
    case BUDDY_HEART:     return 0xFF6B8A;  /* pink */
    default:              return 0xCCCCCC;
    }
}

static const char *state_to_text(buddy_state_t state)
{
    switch (state) {
    case BUDDY_SLEEP:     return "Sleep";
    case BUDDY_IDLE:      return "Idle";
    case BUDDY_BUSY:      return "Busy";
    case BUDDY_ATTENTION: return "Attention!";
    case BUDDY_CELEBRATE: return "Celebrate!";
    case BUDDY_DIZZY:     return "Dizzy...";
    case BUDDY_HEART:     return "<3";
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
    ble_icon = lv_label_create(screen);
    lv_obj_set_style_text_color(ble_icon, lv_color_hex(0x666666), 0);
    lv_label_set_text(ble_icon, "[x]");
    lv_obj_set_style_text_font(ble_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(ble_icon, LV_ALIGN_TOP_LEFT, 8, 6);

    name_label = lv_label_create(screen);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(name_label, "Buddy");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
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
    lv_label_set_text(state_label, "State: Sleep");
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_14, 0);
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_LEFT, 8, -40);

    /* ---- Message label ---- */
    msg_label = lv_label_create(screen);
    lv_obj_set_style_text_color(msg_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(msg_label, "");
    lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_14, 0);
    lv_obj_align(msg_label, LV_ALIGN_BOTTOM_LEFT, 8, -24);
    lv_obj_set_width(msg_label, 224);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_MODE_WRAP);

    /* ---- Navigation hint ---- */
    nav_hint = lv_label_create(screen);
    lv_obj_set_style_text_color(nav_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(nav_hint, "Press:back | SET:info");
    lv_obj_set_style_text_font(nav_hint, &lv_font_montserrat_14, 0);
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
    lv_label_set_text(attn_title, "!! PERMISSION !!");
    lv_obj_set_style_text_font(attn_title, &lv_font_montserrat_16, 0);
    lv_obj_align(attn_title, LV_ALIGN_TOP_MID, 0, 8);

    attn_pet = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_pet, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(attn_pet, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_line_space(attn_pet, 0, 0);
    lv_obj_align(attn_pet, LV_ALIGN_TOP_LEFT, 10, 28);

    attn_tool = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_tool, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(attn_tool, "Tool:");
    lv_obj_set_style_text_font(attn_tool, &lv_font_montserrat_14, 0);
    lv_obj_align(attn_tool, LV_ALIGN_TOP_LEFT, 10, 148);

    attn_hint = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_hint, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_text(attn_hint, "");
    lv_obj_set_style_text_font(attn_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(attn_hint, LV_ALIGN_TOP_LEFT, 10, 166);
    lv_obj_set_width(attn_hint, 216);
    lv_label_set_long_mode(attn_hint, LV_LABEL_LONG_MODE_WRAP);

    attn_approve = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_approve, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(attn_approve, "> Approve");
    lv_obj_set_style_text_font(attn_approve, &lv_font_montserrat_16, 0);
    lv_obj_align(attn_approve, LV_ALIGN_BOTTOM_LEFT, 20, -30);

    attn_deny = lv_label_create(attn_container);
    lv_obj_set_style_text_color(attn_deny, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(attn_deny, "  Deny");
    lv_obj_set_style_text_font(attn_deny, &lv_font_montserrat_16, 0);
    lv_obj_align(attn_deny, LV_ALIGN_BOTTOM_LEFT, 20, -12);

    /* ============================================================
     * INFO mode overlay (initially hidden)
     * ============================================================ */
    info_container = lv_obj_create(screen);
    lv_obj_set_size(info_container, 236, 236);
    lv_obj_align(info_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(info_container, 1, 0);
    lv_obj_set_style_border_color(info_container, lv_color_hex(0x444444), 0);
    lv_obj_add_flag(info_container, LV_OBJ_FLAG_HIDDEN);

    info_title = lv_label_create(info_container);
    lv_obj_set_style_text_color(info_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(info_title, "Buddy Info");
    lv_obj_set_style_text_font(info_title, &lv_font_montserrat_16, 0);
    lv_obj_align(info_title, LV_ALIGN_TOP_MID, 0, 8);

    /* Info fields */
    const char *info_defaults[] = {
        "Pet: --",
        "Owner: --",
        "Approved: 0",
        "Denied: 0",
        "BLE: --",
    };
    for (int i = 0; i < 5; i++) {
        info_labels[i] = lv_label_create(info_container);
        lv_obj_set_style_text_color(info_labels[i], lv_color_hex(0xCCCCCC), 0);
        lv_label_set_text(info_labels[i], info_defaults[i]);
        lv_obj_set_style_text_font(info_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_align(info_labels[i], LV_ALIGN_TOP_LEFT, 16, 34 + i * 20);
    }

    info_action = lv_label_create(info_container);
    lv_obj_set_style_text_color(info_action, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(info_action, "> Next Pet");
    lv_obj_set_style_text_font(info_action, &lv_font_montserrat_14, 0);
    lv_obj_align(info_action, LV_ALIGN_BOTTOM_LEFT, 16, -22);

    info_hint = lv_label_create(info_container);
    lv_obj_set_style_text_color(info_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(info_hint, "Press:back | SET:select");
    lv_obj_set_style_text_font(info_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(info_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

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
        snprintf(buf, sizeof(buf), "State: %s", state_text);
        lv_label_set_text(state_label, buf);
        lv_obj_set_style_text_color(state_label, lv_color_hex(color), 0);
    }

    /* Pet label color */
    if (pet_label) {
        lv_obj_set_style_text_color(pet_label, lv_color_hex(color), 0);
    }

    /* Auto-switch to ATTENTION mode if buddy state demands it */
    if (info.state == BUDDY_ATTENTION && display_mode == MODE_NORMAL && info.has_pending_prompt) {
        /* Will be shown via ui_screen_buddy_show_prompt() from the app layer */
    }

    lvgl_unlock();
}

/* ================================================================
 * Public API: show permission prompt
 * ================================================================ */

void ui_screen_buddy_show_prompt(const char *tool, const char *hint)
{
    lvgl_lock();

    if (attn_tool) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Tool: %s", tool ? tool : "?");
        lv_label_set_text(attn_tool, buf);
    }
    if (attn_hint) {
        lv_label_set_text(attn_hint, hint ? hint : "");
    }
    if (attn_pet) {
        update_attn_pet();
    }

    lvgl_unlock();

    set_display_mode(MODE_ATTENTION);
}

/* ================================================================
 * Public API: clear permission prompt
 * ================================================================ */

void ui_screen_buddy_clear_prompt(void)
{
    set_display_mode(MODE_NORMAL);
}

/* ================================================================
 * Public API: update BLE connection status
 * ================================================================ */

void ui_screen_buddy_set_connected(bool connected)
{
    ble_connected = connected;

    lvgl_lock();
    if (ble_icon) {
        lv_obj_set_style_text_color(ble_icon,
            lv_color_hex(connected ? 0x4D96FF : 0x666666), 0);
        lv_label_set_text(ble_icon, connected ? "[*]" : "[x]");
    }
    lvgl_unlock();
}
