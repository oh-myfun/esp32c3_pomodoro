#include "ui_screen_test.h"
#include "ui_list.h"
#include "ui_manager.h"
#include <stdio.h>

static lv_obj_t *screen = NULL;
static lv_obj_t *test_list = NULL;

static void test_click_cb(int index)
{
    (void)index;
}

lv_obj_t* ui_screen_test_create(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Test List");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    test_list = ui_list_create(screen, 220, 200, 10, 30);

    static ui_list_item_t test_items[10];
    static char item_keys[10][16];
    static char item_vals[10][8];
    for (int i = 0; i < 10; i++) {
        snprintf(item_keys[i], sizeof(item_keys[i]), "Item %d", i + 1);
        item_vals[i][0] = '\0';
        test_items[i].key = item_keys[i];
        test_items[i].value = item_vals[i];
    }
    ui_list_set_items(test_list, test_items, 10);
    ui_list_set_click_callback(test_list, test_click_cb);

    return screen;
}
