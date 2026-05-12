#include "ui_screen_wifi_saved.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_screen_wifi.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/wifi_service.h"
#include "service/storage_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_WIFI_SAVED";

typedef enum {
    SAVED_MODE_LIST = 0,
    SAVED_MODE_ACTION,
} saved_mode_t;

static lv_obj_t *screen = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *hint_label = NULL;
static lv_obj_t *title_label = NULL;

static saved_mode_t mode = SAVED_MODE_LIST;
static int selected_item = 0;
static int action_profile_index = -1;  // Which profile the action menu is for
static int action_selected = 0;
static const int ACTION_COUNT = 3;  // Connect, Edit Password, Delete

#define ITEM_MAX (WIFI_PROFILE_MAX + 1)

static char item_keys[ITEM_MAX][35];
static char item_values[ITEM_MAX][4];
static ui_list_item_t items[ITEM_MAX];
static int item_count = 0;

static char action_keys[3][16];
static char action_values[3][4];
static ui_list_item_t action_items[3];

static void update_action_display(void);

static void update_display(void)
{
    if (mode == SAVED_MODE_ACTION) {
        update_action_display();
        return;
    }

    item_count = 0;

    /* Item 0: always "Scan for new..." */
    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_SCAN_FOR_NEW));
    snprintf(item_values[0], sizeof(item_values[0]), "▸");
    items[0].key = item_keys[0];
    items[0].value = item_values[0];
    item_count = 1;

    /* Items 1..N: saved profiles, mark connected */
    const char *conn_ssid = wifi_service_get_connected_ssid();
    int saved_count = wifi_service_get_saved_count();
    for (int i = 0; i < saved_count && item_count < ITEM_MAX; i++) {
        const char *ssid = wifi_service_get_saved_ssid(i);
        if (ssid) {
            snprintf(item_keys[item_count], sizeof(item_keys[item_count]), "%s", ssid);
            // Show "●" for currently connected network
            if (conn_ssid && strcmp(ssid, conn_ssid) == 0) {
                snprintf(item_values[item_count], sizeof(item_values[item_count]), "●");
            } else {
                item_values[item_count][0] = '\0';
            }
            items[item_count].key = item_keys[item_count];
            items[item_count].value = item_values[item_count];
            item_count++;
        }
    }

    if (wifi_list) {
        ui_list_set_selected_color(wifi_list, lv_color_hex(0x00FF00));
        ui_list_set_items(wifi_list, items, item_count);
        if (selected_item >= item_count) selected_item = 0;
        ui_list_set_selected(wifi_list, selected_item);
    }

    if (hint_label) {
        lv_label_set_text(hint_label, i18n(STR_H_SET_SELECT_PRESS_BACK));
    }
}

static void update_action_display(void)
{
    const char *ssid = wifi_service_get_saved_ssid(action_profile_index);
    if (!ssid) ssid = "?";

    snprintf(action_keys[0], sizeof(action_keys[0]), "%s", i18n(STR_CONNECT));
    action_values[0][0] = '\0';

    snprintf(action_keys[1], sizeof(action_keys[1]), "%s", i18n(STR_EDIT_PASSWORD));
    action_values[1][0] = '\0';

    snprintf(action_keys[2], sizeof(action_keys[2]), "%s", i18n(STR_DELETE));
    action_values[2][0] = '\0';

    for (int i = 0; i < ACTION_COUNT; i++) {
        action_items[i].key = action_keys[i];
        action_items[i].value = action_values[i];
    }

    if (wifi_list) {
        ui_list_set_selected_color(wifi_list, lv_color_hex(0xFFFF00));
        ui_list_set_items(wifi_list, action_items, ACTION_COUNT);
        if (action_selected >= ACTION_COUNT) action_selected = 0;
        ui_list_set_selected(wifi_list, action_selected);
    }

    if (hint_label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s", ssid);
        lv_label_set_text(hint_label, buf);
    }
}

static void saved_on_encoder_cw(void)
{
    if (mode == SAVED_MODE_ACTION) {
        action_selected = (action_selected + 1) % ACTION_COUNT;
        update_action_display();
    } else {
        if (item_count == 0) return;
        selected_item = (selected_item + 1) % item_count;
        ui_list_set_selected(wifi_list, selected_item);
    }
}

static void saved_on_encoder_ccw(void)
{
    if (mode == SAVED_MODE_ACTION) {
        action_selected = (action_selected - 1 + ACTION_COUNT) % ACTION_COUNT;
        update_action_display();
    } else {
        if (item_count == 0) return;
        selected_item = (selected_item - 1 + item_count) % item_count;
        ui_list_set_selected(wifi_list, selected_item);
    }
}

static void saved_on_encoder_press(void)
{
    if (mode == SAVED_MODE_ACTION) {
        mode = SAVED_MODE_LIST;
        update_display();
    } else {
        ui_switch_screen(UI_SCREEN_SETTINGS);
    }
}

static void saved_on_settings_press(void)
{
    if (mode == SAVED_MODE_ACTION) {
        // Execute action
        switch (action_selected) {
            case 0: { // Connect
                char ssid[33] = {0};
                char password[64] = {0};
                if (storage_load_wifi_profile(action_profile_index, ssid, sizeof(ssid), password, sizeof(password))) {
                    wifi_service_connect(ssid, password);
                    mode = SAVED_MODE_LIST;
                    update_display();
                }
                break;
            }
            case 1: { // Edit Password
                const char *ssid = wifi_service_get_saved_ssid(action_profile_index);
                if (ssid) {
                    mode = SAVED_MODE_LIST;
                    ui_switch_screen(UI_SCREEN_PASSWORD_INPUT);
                    ui_screen_password_start(ssid);
                }
                break;
            }
            case 2: { // Delete
                wifi_service_delete_saved(action_profile_index);
                ESP_LOGI(TAG, "Deleted saved WiFi profile at index %d", action_profile_index);
                mode = SAVED_MODE_LIST;
                selected_item = 0;
                update_display();
                break;
            }
        }
    } else {
        if (selected_item == 0) {
            // Scan for new networks
            wifi_service_scan();
            ui_switch_screen(UI_SCREEN_WIFI_LIST);
        } else {
            // Enter action menu for selected saved network
            action_profile_index = selected_item - 1;
            action_selected = 0;
            mode = SAVED_MODE_ACTION;
            update_display();
        }
    }
}

lv_obj_t* ui_screen_wifi_saved_create(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, i18n(STR_T_WIFI));
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, &custom_font_16, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 6);

    wifi_list = ui_list_create(screen, 220, 196, 10, 30);
    ui_list_set_value_width_pct(wifi_list, 15);

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_SELECT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    mode = SAVED_MODE_LIST;
    selected_item = 0;
    update_display();

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = saved_on_encoder_cw,
        .on_encoder_ccw = saved_on_encoder_ccw,
        .on_encoder_press = saved_on_encoder_press,
        .on_settings_press = saved_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_WIFI_SAVED, &cbs);

    ESP_LOGI(TAG, "WiFi Saved screen created");
    return screen;
}

void ui_screen_wifi_saved_refresh(void)
{
    if (title_label) {
        lv_label_set_text(title_label, i18n(STR_T_WIFI));
    }
    if (mode == SAVED_MODE_LIST) {
        update_display();
    }
}
