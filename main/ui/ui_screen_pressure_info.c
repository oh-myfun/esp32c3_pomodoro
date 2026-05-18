#include "ui_screen_pressure_info.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_PRESSURE_INFO";

static lv_obj_t *screen = NULL;
static lv_obj_t *content_label = NULL;
static int scroll_offset = 0;
static int content_height = 0;
#define VIEWPORT_HEIGHT 200
#define SCROLL_STEP 20

static void update_scroll(void)
{
    if (!content_label) return;
    int max_scroll = content_height - VIEWPORT_HEIGHT;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset < 0) scroll_offset = 0;
    if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    lv_obj_set_y(content_label, 30 - scroll_offset);
}

static void on_encoder_cw(void)
{
    scroll_offset += SCROLL_STEP;
    update_scroll();
}

static void on_encoder_ccw(void)
{
    scroll_offset -= SCROLL_STEP;
    update_scroll();
}

static void on_encoder_press(void)
{
    ui_go_back();
}

static void on_settings_press(void)
{
    ui_go_back();
}

static void on_encoder_long_press(void)
{
    ui_go_back();
}

lv_obj_t *ui_screen_pressure_info_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    scroll_offset = 0;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_PRESSURE_INFO));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    /* Scrollable content area */
    lv_obj_t *viewport = lv_obj_create(screen);
    lv_obj_remove_style_all(viewport);
    lv_obj_set_size(viewport, 230, VIEWPORT_HEIGHT);
    lv_obj_align(viewport, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_add_flag(viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);

    content_label = lv_label_create(viewport);
    lv_obj_set_style_text_color(content_label, lv_color_hex(0xFFCC00), 0);
    lv_obj_set_style_text_font(content_label, &custom_font_14, 0);
    lv_obj_set_width(content_label, 226);
    lv_label_set_text(content_label,
        "1013.25hPa = sea level\n"
        "\n"
        "Daily variation\n"
        "  1~4hPa = 8~33m\n"
        "\n"
        "Before rain\n"
        "  -5~15hPa = 42~126m\n"
        "\n"
        "Before storm\n"
        "  -15~30hPa = 126~253m\n"
        "\n"
        "Typhoon\n"
        "  -100+hPa = 868+m\n"
        "\n"
        "Lower pressure = higher\n"
        "altitude reading.\n"
        "Weather changes cause\n"
        "~100m altitude drift."
    );

    lv_obj_set_pos(content_label, 2, 0);
    content_height = lv_obj_get_height(content_label);
    update_scroll();

    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint, i18n(STR_PRESSURE_TIP));
    lv_obj_set_style_text_font(hint, &custom_font_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = on_encoder_cw,
        .on_encoder_ccw = on_encoder_ccw,
        .on_encoder_press = on_encoder_press,
        .on_encoder_long_press = on_encoder_long_press,
        .on_settings_press = on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_PRESSURE_INFO, &cbs);

    ESP_LOGI(TAG, "Pressure info screen created");
    return screen;
}
