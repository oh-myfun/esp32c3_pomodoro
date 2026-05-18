#include "ui_screen_pressure_info.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_PRESSURE_INFO";

static lv_obj_t *screen = NULL;
static lv_obj_t *content = NULL;

static void go_back(void)
{
    ui_go_back();
}

#define SCROLL_STEP 24

static void on_encoder_cw(void)
{
    if (content) lv_obj_scroll_by(content, 0, -SCROLL_STEP, LV_ANIM_OFF);
}

static void on_encoder_ccw(void)
{
    if (content) lv_obj_scroll_by(content, 0, SCROLL_STEP, LV_ANIM_OFF);
}

lv_obj_t *ui_screen_pressure_info_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_PRESSURE_INFO));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    /* Scrollable container: clip children, encoder scrolls this */
    content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 230, 192);
    lv_obj_set_pos(content, 5, 30);
    lv_obj_set_style_bg_opa(content, 0, 0);
    lv_obj_set_style_clip_corner(content, true, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    /* Table content — drawn inside a label with box characters */
    lv_obj_t *table = lv_label_create(content);
    lv_obj_set_style_text_color(table, lv_color_hex(0xFFCC00), 0);
    lv_obj_set_style_text_font(table, &custom_font_14, 0);
    lv_obj_set_width(table, 226);
    lv_label_set_text(table,
        "Std: 1013.25hPa\n"
        "\n"
        "\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x97\n"
        "\xe2\x95\x91 Weather   \xe2\x95\x91 \xe2\x86\x93hPa  \xe2\x95\x91  \xe2\x86\x91m   \xe2\x95\x91\n"
        "\xe2\x95\xa0\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\xa3\n"
        "\xe2\x95\x91 Daily     \xe2\x95\x91  1~4  \xe2\x95\x91  8~33  \xe2\x95\x91\n"
        "\xe2\x95\x91 Rain      \xe2\x95\x91  5~15 \xe2\x95\x91 42~126 \xe2\x95\x91\n"
        "\xe2\x95\x91 Storm     \xe2\x95\x91 15~30 \xe2\x95\x91 126~253\xe2\x95\x91\n"
        "\xe2\x95\x91 Typhoon   \xe2\x95\x91 100+  \xe2\x95\x91  868+  \xe2\x95\x91\n"
        "\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
        "\xe2\x95\x9d\n"
        "\n"
        "Lower pressure =\n"
        "higher altitude.\n"
        "Weather can cause\n"
        "~100m altitude drift."
    );

    lv_obj_set_pos(table, 2, 0);

    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint, i18n(STR_PRESSURE_TIP));
    lv_obj_set_style_text_font(hint, &custom_font_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = on_encoder_cw,
        .on_encoder_ccw = on_encoder_ccw,
        .on_encoder_press = go_back,
        .on_encoder_long_press = go_back,
        .on_settings_press = go_back,
    };
    ui_register_input_callbacks(UI_SCREEN_PRESSURE_INFO, &cbs);

    ESP_LOGI(TAG, "Pressure info screen created");
    return screen;
}
