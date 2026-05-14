#include "ui_screen_settings_bridge.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/tcp_service.h"
#include "service/storage_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_SETTINGS_BRIDGE";

typedef enum {
    BRIDGE_MODE_NAV = 0,    /* Encoder rotates to navigate items */
    BRIDGE_MODE_ADJUST,     /* Encoder rotates to adjust value */
} bridge_edit_mode_t;

enum {
    ITEM_HOST = 0,
    ITEM_PORT,
    ITEM_PAIRING_CODE,
    ITEM_CONNECT,
    ITEM_BACK,
    ITEM_COUNT
};

#define HOST_MAX_LEN    47
#define PORT_MIN        1
#define PORT_MAX        65535
#define CODE_MAX_LEN    8

/* Pairing code char set: 0-9 A-Z */
static const char code_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
#define CODE_CHARS_COUNT 36

static bridge_edit_mode_t bridge_mode = BRIDGE_MODE_NAV;
static int selected_item = 0;

/* Config state */
static char cfg_host[HOST_MAX_LEN + 1] = "";
static int  cfg_port = 9876;
static char cfg_pairing_code[CODE_MAX_LEN + 1] = "";

/* Adjust state for pairing code input */
static int  code_cursor = 0;        /* which char position (0..CODE_MAX_LEN-1) */

static lv_obj_t *screen = NULL;
static lv_obj_t *bridge_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[ITEM_COUNT][24];
static char item_values[ITEM_COUNT][16];
static ui_list_item_t items[ITEM_COUNT];

static void load_config(void)
{
    tcp_service_load_config(cfg_host, sizeof(cfg_host), &cfg_port);
    if (!tcp_service_load_pairing_code(cfg_pairing_code, sizeof(cfg_pairing_code))) {
        cfg_pairing_code[0] = '\0';
    }
}

static void save_config(void)
{
    tcp_service_save_config(cfg_host, cfg_port);
}

static void save_pairing_code(void)
{
    tcp_service_save_pairing_code(cfg_pairing_code);
}

static void update_display(void);

/* ---- Input callbacks ---- */

static void bridge_on_encoder_cw(void)
{
    if (bridge_mode == BRIDGE_MODE_NAV) {
        selected_item = (selected_item + 1) % ITEM_COUNT;
        update_display();
    } else if (bridge_mode == BRIDGE_MODE_ADJUST) {
        switch (selected_item) {
            case ITEM_PORT:
                if (cfg_port < PORT_MAX) {
                    cfg_port++;
                }
                break;
            case ITEM_PAIRING_CODE: {
                int len = (int)strlen(cfg_pairing_code);
                /* Extend string if cursor is at end */
                if (code_cursor >= len && len < CODE_MAX_LEN) {
                    cfg_pairing_code[code_cursor] = '0';
                    cfg_pairing_code[code_cursor + 1] = '\0';
                }
                if (code_cursor < CODE_MAX_LEN && cfg_pairing_code[code_cursor]) {
                    /* Find current char in set, advance */
                    char *p = strchr(code_chars, cfg_pairing_code[code_cursor]);
                    if (p && *(p + 1)) {
                        cfg_pairing_code[code_cursor] = *(p + 1);
                    } else {
                        cfg_pairing_code[code_cursor] = code_chars[0];
                    }
                }
                break;
            }
        }
        update_display();
    }
}

static void bridge_on_encoder_ccw(void)
{
    if (bridge_mode == BRIDGE_MODE_NAV) {
        selected_item = (selected_item - 1 + ITEM_COUNT) % ITEM_COUNT;
        update_display();
    } else if (bridge_mode == BRIDGE_MODE_ADJUST) {
        switch (selected_item) {
            case ITEM_PORT:
                if (cfg_port > PORT_MIN) {
                    cfg_port--;
                }
                break;
            case ITEM_PAIRING_CODE: {
                int len = (int)strlen(cfg_pairing_code);
                if (code_cursor < len && cfg_pairing_code[code_cursor]) {
                    char *p = strchr(code_chars, cfg_pairing_code[code_cursor]);
                    if (p && p > code_chars) {
                        cfg_pairing_code[code_cursor] = *(p - 1);
                    } else {
                        cfg_pairing_code[code_cursor] = code_chars[CODE_CHARS_COUNT - 1];
                    }
                }
                break;
            }
        }
        update_display();
    }
}

static void bridge_on_encoder_press(void)
{
    if (bridge_mode == BRIDGE_MODE_ADJUST) {
        /* In adjust mode, encoder press cancels without saving */
        bridge_mode = BRIDGE_MODE_NAV;
        load_config();
        update_display();
    } else {
        ui_go_back();
    }
}

static void bridge_on_settings_press(void)
{
    if (bridge_mode == BRIDGE_MODE_NAV) {
        switch (selected_item) {
            case ITEM_HOST:
                /* Host editing deferred — do nothing */
                break;
            case ITEM_PORT:
                bridge_mode = BRIDGE_MODE_ADJUST;
                update_display();
                break;
            case ITEM_PAIRING_CODE:
                /* Enter adjust mode at first char position */
                code_cursor = 0;
                /* Pad with '0' if empty */
                if (cfg_pairing_code[0] == '\0') {
                    memset(cfg_pairing_code, '0', CODE_MAX_LEN);
                    cfg_pairing_code[CODE_MAX_LEN] = '\0';
                }
                bridge_mode = BRIDGE_MODE_ADJUST;
                update_display();
                break;
            case ITEM_CONNECT:
                if (tcp_service_is_connected()) {
                    tcp_service_disconnect();
                } else {
                    save_config();
                    tcp_service_connect(cfg_host, cfg_port);
                }
                update_display();
                break;
            case ITEM_BACK:
                ui_go_back();
                break;
        }
    } else {
        /* In adjust mode — SET key saves */
        switch (selected_item) {
            case ITEM_PORT:
                save_config();
                ESP_LOGI(TAG, "Port saved: %d", cfg_port);
                break;
            case ITEM_PAIRING_CODE:
                /* Move to next char position, or save if at end */
                if (code_cursor < CODE_MAX_LEN - 1) {
                    code_cursor++;
                    update_display();
                    return;
                }
                save_pairing_code();
                ESP_LOGI(TAG, "Pairing code saved: %s", cfg_pairing_code);
                break;
        }
        bridge_mode = BRIDGE_MODE_NAV;
        update_display();
    }
}

static void bridge_on_encoder_long_press(void)
{
    if (bridge_mode == BRIDGE_MODE_ADJUST) {
        bridge_mode = BRIDGE_MODE_NAV;
        load_config();
        ESP_LOGI(TAG, "Cancelled changes");
        update_display();
    } else {
        ui_go_back();
    }
}

/* ---- Display ---- */

static void update_display(void)
{
    /* Item 0: Host */
    snprintf(item_keys[ITEM_HOST], sizeof(item_keys[ITEM_HOST]), "Host");
    snprintf(item_values[ITEM_HOST], sizeof(item_values[ITEM_HOST]),
             "%s", cfg_host[0] ? cfg_host : "---");

    /* Item 1: Port */
    snprintf(item_keys[ITEM_PORT], sizeof(item_keys[ITEM_PORT]), "Port");
    snprintf(item_values[ITEM_PORT], sizeof(item_values[ITEM_PORT]), "%d", cfg_port);

    /* Item 2: Pairing code */
    snprintf(item_keys[ITEM_PAIRING_CODE], sizeof(item_keys[ITEM_PAIRING_CODE]), "Code");
    if (bridge_mode == BRIDGE_MODE_ADJUST && selected_item == ITEM_PAIRING_CODE) {
        /* Show cursor position with marker */
        char display_code[CODE_MAX_LEN + 3]; /* brackets + null */
        int len = (int)strlen(cfg_pairing_code);
        int pos = 0;
        for (int i = 0; i < len && i < CODE_MAX_LEN; i++) {
            if (i == code_cursor) {
                pos += snprintf(display_code + pos, sizeof(display_code) - pos,
                                "[%c]", cfg_pairing_code[i]);
            } else {
                display_code[pos++] = cfg_pairing_code[i];
            }
        }
        display_code[pos] = '\0';
        snprintf(item_values[ITEM_PAIRING_CODE], sizeof(item_values[ITEM_PAIRING_CODE]),
                 "%s", display_code);
    } else {
        snprintf(item_values[ITEM_PAIRING_CODE], sizeof(item_values[ITEM_PAIRING_CODE]),
                 "%s", cfg_pairing_code[0] ? cfg_pairing_code : "---");
    }

    /* Item 3: Connect / Disconnect */
    snprintf(item_keys[ITEM_CONNECT], sizeof(item_keys[ITEM_CONNECT]),
             "%s", tcp_service_is_connected() ? "Disconnect" : "Connect");
    snprintf(item_values[ITEM_CONNECT], sizeof(item_values[ITEM_CONNECT]),
             "%s", tcp_service_is_connected() ? "ON" : "OFF");

    /* Item 4: Back */
    snprintf(item_keys[ITEM_BACK], sizeof(item_keys[ITEM_BACK]), "%s", i18n(STR_BACK));
    snprintf(item_values[ITEM_BACK], sizeof(item_values[ITEM_BACK]), "");

    for (int i = 0; i < ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (bridge_list) {
        if (bridge_mode == BRIDGE_MODE_ADJUST) {
            ui_list_set_selected_color(bridge_list, lv_color_hex(0xFFFF00));
        } else {
            ui_list_set_selected_color(bridge_list, lv_color_hex(0x00FF00));
        }
        ui_list_set_items(bridge_list, items, ITEM_COUNT);
        ui_list_set_selected(bridge_list, selected_item);
    }

    if (hint_label) {
        if (bridge_mode == BRIDGE_MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
        }
    }
}

/* ---- Public API ---- */

lv_obj_t *ui_screen_settings_bridge_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    bridge_list = NULL;
    hint_label = NULL;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Bridge");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    bridge_list = ui_list_create(screen, 220, 196, 10, 30);

    /* Load config and display */
    load_config();
    bridge_mode = BRIDGE_MODE_NAV;
    selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = bridge_on_encoder_cw,
        .on_encoder_ccw = bridge_on_encoder_ccw,
        .on_encoder_press = bridge_on_encoder_press,
        .on_encoder_long_press = bridge_on_encoder_long_press,
        .on_settings_press = bridge_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_BRIDGE, &cbs);

    ESP_LOGI(TAG, "Settings Bridge screen created");
    return screen;
}

void ui_screen_settings_bridge_refresh(void)
{
    load_config();
    update_display();
}
