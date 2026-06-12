#include "ui_screen_settings_sound_demo.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/sound_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SOUND_DEMO";

#define SOUND_DEMO_COUNT   (SOUND_COUNT + 2)   /* 19 普通音效 + 整点 + 半点 */
#define HOUR_CHIME_IDX     SOUND_COUNT          /* 19 */
#define HALF_CHIME_IDX     (SOUND_COUNT + 1)    /* 20 */

typedef enum { MODE_NAV, MODE_ADJUST } sound_demo_mode_t;

static lv_obj_t *screen = NULL;
static lv_obj_t *demo_list = NULL;
static lv_obj_t *hint_label = NULL;

static sound_demo_mode_t mode = MODE_NAV;
static int selected = 0;
static int hour12_val = 3;   /* 整点响数 (1..12)，ADJUST 模式可调 */

static char item_keys[SOUND_DEMO_COUNT][20];
static char item_values[SOUND_DEMO_COUNT][12];
static ui_list_item_t items[SOUND_DEMO_COUNT];

/* 索引 0..SOUND_COUNT-1 与 sound_id_t 一一对应 */
static const str_id_t sound_name_ids[SOUND_COUNT] = {
    STR_SOUND_KEY_CLICK, STR_SOUND_CONFIRM, STR_SOUND_CANCEL,
    STR_SOUND_SUCCESS,   STR_SOUND_FAIL,    STR_SOUND_WIFI_CONNECT,
    STR_SOUND_WIFI_CONNECTED, STR_SOUND_WIFI_FAILED, STR_SOUND_SYNC_START,
    STR_SOUND_SYNC_DONE, STR_SOUND_POMO_START, STR_SOUND_POMO_WORK_START,
    STR_SOUND_POMO_BREAK_START, STR_SOUND_POMO_WORK_DONE,
    STR_SOUND_POMO_BREAK_DONE, STR_SOUND_POMO_LONG_BREAK,
    STR_SOUND_BUDDY_ATTENTION, STR_SOUND_BUDDY_HAPPY, STR_SOUND_BUDDY_SAD,
};

static void update_display(void)
{
    for (int i = 0; i < SOUND_COUNT; i++) {
        snprintf(item_keys[i], sizeof(item_keys[i]), "%s", i18n(sound_name_ids[i]));
        snprintf(item_values[i], sizeof(item_values[i]), " ");
    }

    snprintf(item_keys[HOUR_CHIME_IDX], sizeof(item_keys[HOUR_CHIME_IDX]),
             "%s", i18n(STR_DEMO_HOUR_CHIME));
    snprintf(item_values[HOUR_CHIME_IDX], sizeof(item_values[HOUR_CHIME_IDX]),
             i18n(STR_FMT_CHIME_COUNT), hour12_val);

    snprintf(item_keys[HALF_CHIME_IDX], sizeof(item_keys[HALF_CHIME_IDX]),
             "%s", i18n(STR_DEMO_HALF_CHIME));
    snprintf(item_values[HALF_CHIME_IDX], sizeof(item_values[HALF_CHIME_IDX]), " ");

    for (int i = 0; i < SOUND_DEMO_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (demo_list) {
        ui_list_set_selected_color(demo_list,
            mode == MODE_ADJUST ? lv_color_hex(0xFFAA00) : lv_color_hex(0x00FF00));
        ui_list_set_items(demo_list, items, SOUND_DEMO_COUNT);
        ui_list_set_selected(demo_list, selected);
    }

    if (hint_label) {
        if (mode == MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SELECT_PRESS_BACK));
        }
    }
}

static void demo_on_encoder_cw(void)
{
    int step = ui_get_encoder_step();
    if (mode == MODE_ADJUST) {
        hour12_val = (hour12_val - 1 + step) % 12 + 1;  /* 1..12 wrap */
        update_display();
    } else {
        selected = (selected + 1) % SOUND_DEMO_COUNT;
        update_display();
    }
}

static void demo_on_encoder_ccw(void)
{
    int step = ui_get_encoder_step();
    if (mode == MODE_ADJUST) {
        hour12_val = ((hour12_val - 1 - step + 12) % 12) + 1;
        update_display();
    } else {
        selected = (selected - 1 + SOUND_DEMO_COUNT) % SOUND_DEMO_COUNT;
        update_display();
    }
}

static void demo_on_encoder_press(void)
{
    if (mode == MODE_ADJUST) {
        /* 取消：退出 ADJUST 不播放 */
        mode = MODE_NAV;
        update_display();
    } else {
        ui_go_back();
    }
}

static void demo_on_settings_press(void)
{
    if (mode == MODE_ADJUST) {
        /* 播放整点响 hour12 次并退出 ADJUST */
        sound_service_play_hour_chime_raw(hour12_val);
        mode = MODE_NAV;
        update_display();
        return;
    }

    if (selected < SOUND_COUNT) {
        /* 普通音效：立即播放（绕过开关） */
        sound_service_play_raw((sound_id_t)selected);
    } else if (selected == HOUR_CHIME_IDX) {
        /* 整点：进入 ADJUST 调响数 */
        mode = MODE_ADJUST;
        update_display();
    } else if (selected == HALF_CHIME_IDX) {
        /* 半点：立即播放（绕过开关） */
        sound_service_play_half_chime_raw();
    }
}

lv_obj_t *ui_screen_settings_sound_demo_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    demo_list = NULL;
    hint_label = NULL;
    mode = MODE_NAV;
    selected = 0;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_DEMO));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    demo_list = ui_list_create(screen, 220, 196, 10, 30);

    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_SELECT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = demo_on_encoder_cw,
        .on_encoder_ccw = demo_on_encoder_ccw,
        .on_encoder_press = demo_on_encoder_press,
        .on_encoder_long_press = NULL,
        .on_settings_press = demo_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_SOUND_DEMO, &cbs);

    ESP_LOGI(TAG, "Sound demo screen created (%d items)", SOUND_DEMO_COUNT);
    return screen;
}
