#include "ui_screen_settings_buddy.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "ui_text_input.h"
#include "buddy/buddy.h"
#include "service/tcp_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "UI_BUDDY_SET";

enum {
    ITEM_SPECIES = 0,
    ITEM_HOST,
    ITEM_PORT,
    ITEM_SESSION,
    ITEM_SCAN,
    ITEM_CONNECT,
    ITEM_COUNT   /* = 6 */
};

typedef enum {
    BUDDY_MODE_NAV = 0,
    BUDDY_MODE_ADJUST,   /* only for species */
} buddy_edit_mode_t;

static lv_obj_t *screen = NULL;
static lv_obj_t *buddy_list = NULL;
static lv_obj_t *hint_label = NULL;

static buddy_edit_mode_t edit_mode = BUDDY_MODE_NAV;
static int selected_item = 0;

/* Species */
static int species_index = 0;

/* Bridge config */
static char cfg_host[48] = "";
static int  cfg_port = 9876;
static char cfg_session[9] = "";  /* 8 chars + NUL */

/* List display buffers */
static char item_keys[ITEM_COUNT][24];
static char item_values[ITEM_COUNT][48];
static ui_list_item_t items[ITEM_COUNT];

static void load_config(void)
{
    buddy_info_t info = buddy_get_info();
    species_index = info.species_index;

    tcp_service_load_config(cfg_host, sizeof(cfg_host), &cfg_port);
    if (!tcp_service_load_pairing_code(cfg_session, sizeof(cfg_session))) {
        cfg_session[0] = '\0';
    }
}

static void update_display(void);

static void on_host_result(const char *result)
{
    if (result && result[0]) {
        strncpy(cfg_host, result, sizeof(cfg_host) - 1);
        cfg_host[sizeof(cfg_host) - 1] = '\0';
        tcp_service_save_config(cfg_host, cfg_port);
    }
}

static void on_port_result(const char *result)
{
    if (result && result[0]) {
        int port = atoi(result);
        if (port >= 1 && port <= 65535) {
            cfg_port = port;
            tcp_service_save_config(cfg_host, cfg_port);
        }
    }
}

static void on_session_result(const char *result)
{
    if (result && result[0]) {
        strncpy(cfg_session, result, sizeof(cfg_session) - 1);
        cfg_session[sizeof(cfg_session) - 1] = '\0';
        tcp_service_save_pairing_code(cfg_session);
    }
}

static void update_display(void)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    /* Item 0: Species */
    snprintf(item_keys[ITEM_SPECIES], sizeof(item_keys[ITEM_SPECIES]), "%s", i18n(STR_SPECIES));
    snprintf(item_values[ITEM_SPECIES], sizeof(item_values[ITEM_SPECIES]), "%s", buddy_get_species_name(species_index));

    /* Item 1: Host */
    snprintf(item_keys[ITEM_HOST], sizeof(item_keys[ITEM_HOST]), "%s", i18n(STR_HOST));
    snprintf(item_values[ITEM_HOST], sizeof(item_values[ITEM_HOST]), "%s", cfg_host[0] ? cfg_host : "---");

    /* Item 2: Port */
    snprintf(item_keys[ITEM_PORT], sizeof(item_keys[ITEM_PORT]), "%s", i18n(STR_PORT));
    snprintf(item_values[ITEM_PORT], sizeof(item_values[ITEM_PORT]), "%d", cfg_port);

    /* Item 3: Session */
    snprintf(item_keys[ITEM_SESSION], sizeof(item_keys[ITEM_SESSION]), "%s", i18n(STR_SESSION));
    snprintf(item_values[ITEM_SESSION], sizeof(item_values[ITEM_SESSION]), "%s", cfg_session[0] ? cfg_session : "---");

    /* Item 4: Scan */
    snprintf(item_keys[ITEM_SCAN], sizeof(item_keys[ITEM_SCAN]), "%s", i18n(STR_SCAN));
    snprintf(item_values[ITEM_SCAN], sizeof(item_values[ITEM_SCAN]), "%s", "\xe2\x87\xa8");  /* ⇨ */

    /* Item 5: Connect/Disconnect */
    snprintf(item_keys[ITEM_CONNECT], sizeof(item_keys[ITEM_CONNECT]), "%s", tcp_service_is_connected() ? i18n(STR_DISCONNECT) : i18n(STR_CONNECT_ACTION));
    snprintf(item_values[ITEM_CONNECT], sizeof(item_values[ITEM_CONNECT]), "%s", tcp_service_is_connected() ? i18n(STR_ON) : i18n(STR_OFF));
#pragma GCC diagnostic pop

    for (int i = 0; i < ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (buddy_list) {
        lv_color_t color = (edit_mode == BUDDY_MODE_ADJUST) ? lv_color_hex(0xFFFF00) : lv_color_hex(0x00FF00);
        ui_list_set_selected_color(buddy_list, color);
        ui_list_set_items(buddy_list, items, ITEM_COUNT);
        ui_list_set_selected(buddy_list, selected_item);
    }

    if (hint_label) {
        if (edit_mode == BUDDY_MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
        }
    }
}

static void buddy_on_encoder_cw(void)
{
    if (edit_mode == BUDDY_MODE_NAV) {
        selected_item = (selected_item + 1) % ITEM_COUNT;
        update_display();
    } else if (edit_mode == BUDDY_MODE_ADJUST && selected_item == ITEM_SPECIES) {
        int count = buddy_get_species_count();
        species_index = (species_index + 1) % count;
        update_display();
    }
}

static void buddy_on_encoder_ccw(void)
{
    if (edit_mode == BUDDY_MODE_NAV) {
        selected_item = (selected_item - 1 + ITEM_COUNT) % ITEM_COUNT;
        update_display();
    } else if (edit_mode == BUDDY_MODE_ADJUST && selected_item == ITEM_SPECIES) {
        int count = buddy_get_species_count();
        species_index = (species_index - 1 + count) % count;
        update_display();
    }
}

static void buddy_on_encoder_press(void)
{
    if (edit_mode == BUDDY_MODE_ADJUST) {
        /* Cancel adjust */
        edit_mode = BUDDY_MODE_NAV;
        load_config();  /* reload original value */
        update_display();
    } else {
        ui_go_back();
    }
}

static void buddy_on_encoder_long_press(void)
{
    if (edit_mode == BUDDY_MODE_ADJUST) {
        edit_mode = BUDDY_MODE_NAV;
        load_config();
        update_display();
    } else {
        ui_go_back();
    }
}

static void buddy_on_settings_press(void)
{
    if (edit_mode == BUDDY_MODE_ADJUST) {
        /* Save species */
        if (selected_item == ITEM_SPECIES) {
            buddy_set_species(species_index);
        }
        edit_mode = BUDDY_MODE_NAV;
        update_display();
        return;
    }

    /* NAV mode: enter subpage or toggle */
    switch (selected_item) {
        case ITEM_SPECIES:
            edit_mode = BUDDY_MODE_ADJUST;
            update_display();
            break;
        case ITEM_HOST:
            ui_text_input_configure(i18n(STR_HOST), cfg_host, TEXT_INPUT_IP, 15, on_host_result);
            ui_switch_screen(UI_SCREEN_TEXT_INPUT);
            break;
        case ITEM_PORT: {
            char port_str[6];
            snprintf(port_str, sizeof(port_str), "%d", cfg_port);
            ui_text_input_configure(i18n(STR_PORT), port_str, TEXT_INPUT_PORT, 5, on_port_result);
            ui_switch_screen(UI_SCREEN_TEXT_INPUT);
            break;
        }
        case ITEM_SESSION:
            ui_text_input_configure(i18n(STR_SESSION), cfg_session, TEXT_INPUT_ALPHANUM, 8, on_session_result);
            ui_switch_screen(UI_SCREEN_TEXT_INPUT);
            break;
        case ITEM_SCAN:
            ui_switch_screen(UI_SCREEN_BRIDGE_SCAN);
            break;
        case ITEM_CONNECT:
            if (tcp_service_is_connected()) {
                tcp_service_disconnect();
            } else {
                tcp_service_connect(cfg_host, cfg_port);
            }
            update_display();
            break;
    }
}

lv_obj_t *ui_screen_settings_buddy_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    buddy_list = NULL;
    hint_label = NULL;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_T_BUDDY));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    buddy_list = ui_list_create(screen, 220, 196, 10, 30);
    ui_list_set_value_width_pct(buddy_list, 60);

    load_config();
    edit_mode = BUDDY_MODE_NAV;
    selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = buddy_on_encoder_cw,
        .on_encoder_ccw = buddy_on_encoder_ccw,
        .on_encoder_press = buddy_on_encoder_press,
        .on_encoder_long_press = buddy_on_encoder_long_press,
        .on_settings_press = buddy_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_BUDDY, &cbs);

    ESP_LOGI(TAG, "Buddy settings screen created");
    return screen;
}

void ui_screen_settings_buddy_refresh(void)
{
    /* Only update when connect state actually changed */
    bool now_connected = tcp_service_is_connected();
    static bool last_connected = false;
    if (now_connected != last_connected) {
        last_connected = now_connected;
        update_display();
    }
}

void ui_screen_settings_buddy_set_scan_result(const char *host, int port, const char *pairing_code)
{
    if (host && host[0]) {
        strncpy(cfg_host, host, sizeof(cfg_host) - 1);
        cfg_host[sizeof(cfg_host) - 1] = '\0';
        cfg_port = port;
        tcp_service_save_config(cfg_host, cfg_port);
    }
    if (pairing_code && pairing_code[0]) {
        strncpy(cfg_session, pairing_code, sizeof(cfg_session) - 1);
        cfg_session[sizeof(cfg_session) - 1] = '\0';
        tcp_service_save_pairing_code(cfg_session);
    }
}
