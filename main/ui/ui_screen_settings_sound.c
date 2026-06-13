#include "ui_screen_settings_sound.h"
#include "custom_font.h"
#include "i18n.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/storage_service.h"
#include "service/sound_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_SOUND";

#define SOUND_ITEM_COUNT 11

/* 索引含义：
 *   0..7  开关项（总开关 + 7 分类）
 *   8     Quiet Start
 *   9     Quiet End
 *   10    Demo (push 到演示子界面，当前为空列表用于排查)
 */
typedef enum { MODE_NAV, MODE_ADJUST } sound_mode_t;

static sound_mode_t sound_mode = MODE_NAV;
static int selected_item = 0;
static bool bool_vals[8];     /* items 0..7 */
static int  quiet_vals[2];    /* items 8,9: start,end */

static lv_obj_t *screen = NULL;
static lv_obj_t *sound_list = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[SOUND_ITEM_COUNT][20];
static char item_values[SOUND_ITEM_COUNT][12];
static ui_list_item_t items[SOUND_ITEM_COUNT];

static const str_id_t bool_key_ids[8] = {
    STR_T_SOUND, STR_SND_KEY, STR_SND_UI, STR_SND_NET,
    STR_SND_POMO, STR_SND_BUDDY, STR_SND_HOUR, STR_SND_HALF,
};
static const sound_category_t cat_for_item[8] = {
    SND_CAT_KEY,        /* index 0 = 总开关, 占位无意义（实际走 set_enabled） */
    SND_CAT_KEY,
    SND_CAT_UI,
    SND_CAT_NET,
    SND_CAT_POMO,
    SND_CAT_BUDDY,
    SND_CAT_HOUR_CHIME,
    SND_CAT_HALF_CHIME,
};

static void load_values(void)
{
    bool_vals[0] = sound_service_is_enabled();
    for (int i = 1; i < 8; i++) {
        bool_vals[i] = sound_service_is_category_enabled(cat_for_item[i]);
    }
    int qs, qe;
    sound_service_get_quiet_range(&qs, &qe);
    quiet_vals[0] = qs;
    quiet_vals[1] = qe;
}

static void update_display(void)
{
    const char *on_off[] = {i18n(STR_OFF), i18n(STR_ON)};

    for (int i = 0; i < 8; i++) {
        snprintf(item_keys[i], sizeof(item_keys[i]), "%s", i18n(bool_key_ids[i]));
        snprintf(item_values[i], sizeof(item_values[i]), "%s", on_off[bool_vals[i] ? 1 : 0]);
    }
    snprintf(item_keys[8], sizeof(item_keys[8]), "%s", i18n(STR_QUIET_START));
    snprintf(item_values[8], sizeof(item_values[8]), i18n(STR_FMT_HOUR), quiet_vals[0]);
    snprintf(item_keys[9], sizeof(item_keys[9]), "%s", i18n(STR_QUIET_END));
    snprintf(item_values[9], sizeof(item_values[9]), i18n(STR_FMT_HOUR), quiet_vals[1]);
    snprintf(item_keys[10], sizeof(item_keys[10]), "%s", i18n(STR_SND_DEMO));
    snprintf(item_values[10], sizeof(item_values[10]), ">>");

    for (int i = 0; i < SOUND_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (sound_list) {
        ui_list_set_selected_color(sound_list,
            sound_mode == MODE_ADJUST ? lv_color_hex(0xFFAA00) : lv_color_hex(0x00FF00));
        ui_list_set_items(sound_list, items, SOUND_ITEM_COUNT);
        ui_list_set_selected(sound_list, selected_item);
    }

    if (hint_label) {
        if (sound_mode == MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SET_SAVE_PRESS_CANCEL));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_TOGGLE_PRESS_BACK));
        }
    }
}

static void sound_on_encoder_cw(void)
{
    int step = ui_get_encoder_step();
    if (sound_mode == MODE_ADJUST) {
        if (selected_item == 8 || selected_item == 9) {
            quiet_vals[selected_item - 8] = (quiet_vals[selected_item - 8] + step) % 24;
        }
        update_display();
    } else {
        selected_item = (selected_item + 1) % SOUND_ITEM_COUNT;
        update_display();
    }
}

static void sound_on_encoder_ccw(void)
{
    int step = ui_get_encoder_step();
    if (sound_mode == MODE_ADJUST) {
        if (selected_item == 8 || selected_item == 9) {
            quiet_vals[selected_item - 8] = (quiet_vals[selected_item - 8] - step + 24) % 24;
        }
        update_display();
    } else {
        selected_item = (selected_item - 1 + SOUND_ITEM_COUNT) % SOUND_ITEM_COUNT;
        update_display();
    }
}

static void sound_on_encoder_press(void)
{
    if (sound_mode == MODE_ADJUST) {
        /* 取消：恢复加载的值 */
        load_values();
        sound_mode = MODE_NAV;
        update_display();
    } else {
        ui_go_back();
    }
}

static void sound_on_settings_press(void)
{
    if (sound_mode == MODE_ADJUST) {
        /* 保存静默时段并退出 ADJUST */
        sound_service_set_quiet_range(quiet_vals[0], quiet_vals[1]);
        sound_mode = MODE_NAV;
        update_display();
        return;
    }

    if (selected_item == 0) {
        /* 总开关 */
        bool_vals[0] = !bool_vals[0];
        sound_service_set_enabled(bool_vals[0]);
    } else if (selected_item >= 1 && selected_item <= 7) {
        /* 分类开关 */
        bool_vals[selected_item] = !bool_vals[selected_item];
        sound_service_set_category_enabled(cat_for_item[selected_item], bool_vals[selected_item]);
    } else if (selected_item == 8 || selected_item == 9) {
        /* 8 / 9 = 进入 ADJUST */
        sound_mode = MODE_ADJUST;
    } else if (selected_item == 10) {
        /* Demo = 进入演示子界面。
         * push 后不要调用 update_display：ui_push_screen 会 pre-clean 当前
         * SETTINGS_SOUND 屏幕（删除 sound_list 等子对象），此时 sound_list
         * 已是悬垂指针，再调 update_display 会解引用已释放内存导致崩溃。 */
        ui_push_screen(UI_SCREEN_SETTINGS_SOUND_DEMO);
        return;
    }
    update_display();
}

lv_obj_t* ui_screen_settings_sound_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    sound_list = NULL;
    hint_label = NULL;
    sound_mode = MODE_NAV;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_T_SOUND));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    sound_list = ui_list_create(screen, 220, 196, 10, 30);

    load_values();
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_TOGGLE_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = sound_on_encoder_cw,
        .on_encoder_ccw = sound_on_encoder_ccw,
        .on_encoder_press = sound_on_encoder_press,
        .on_encoder_long_press = NULL,
        .on_settings_press = sound_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_SOUND, &cbs);

    ESP_LOGI(TAG, "Settings Sound screen created");
    return screen;
}

void ui_screen_settings_sound_refresh(void)
{
    load_values();
    sound_mode = MODE_NAV;
    update_display();
}
