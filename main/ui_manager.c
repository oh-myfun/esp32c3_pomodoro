#include "ui_manager.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

static const char *TAG = "UI";

static lv_obj_t *screens[UI_SCREEN_COUNT];
static ui_screen_id_t current_screen = UI_SCREEN_MAIN;
static lv_obj_t *time_label = NULL;

// 主界面 - 时间显示
lv_obj_t *ui_create_main_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_size(screen, 240, 240);
    
    // 创建时间标签
    time_label = lv_label_create(screen);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(time_label, "12:00");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(time_label, 8, 0);
    lv_obj_center(time_label);
    
    // 创建日期标签
    lv_obj_t *date_label = lv_label_create(screen);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(date_label, "2025-01-01");
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 30);
    
    // 创建指示器标签
    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(hint, "Rotate to switch");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    return screen;
}

// 温度界面
lv_obj_t *ui_create_temp_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_size(screen, 240, 240);
    
    // 标题
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "TEMP");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    // 温度值
    lv_obj_t *temp = lv_label_create(screen);
    lv_obj_set_style_text_color(temp, lv_color_hex(0xFF6B6B), 0);
    lv_label_set_text(temp, "25.5 C");
    lv_obj_set_style_text_font(temp, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(temp, 4, 0);
    lv_obj_center(temp);
    
    return screen;
}

// 湿度界面
lv_obj_t *ui_create_humidity_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x16213e), 0);
    lv_obj_set_size(screen, 240, 240);
    
    // 标题
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "HUMIDITY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    // 湿度值
    lv_obj_t *humidity = lv_label_create(screen);
    lv_obj_set_style_text_color(humidity, lv_color_hex(0x4D96FF), 0);
    lv_label_set_text(humidity, "65%");
    lv_obj_set_style_text_font(humidity, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(humidity, 6, 0);
    lv_obj_center(humidity);
    
    return screen;
}

// 设置界面
lv_obj_t *ui_create_settings_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_size(screen, 240, 240);
    
    // 标题
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // 创建设置列表
    lv_obj_t *list = lv_list_create(screen);
    lv_obj_set_size(list, 200, 140);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x3d3d3d), 0);
    
    // 添加设置项
    lv_obj_t *btn1 = lv_list_add_btn(list, NULL, "Brightness");
    lv_obj_t *btn2 = lv_list_add_btn(list, NULL, "Contrast");
    lv_obj_t *btn3 = lv_list_add_btn(list, NULL, "Language");
    
    return screen;
}

void ui_init(void)
{
    // 创建所有界面
    screens[UI_SCREEN_MAIN] = ui_create_main_screen();
    screens[UI_SCREEN_TEMP] = ui_create_temp_screen();
    screens[UI_SCREEN_HUMIDITY] = ui_create_humidity_screen();
    screens[UI_SCREEN_SETTINGS] = ui_create_settings_screen();
    
    // 加载主界面（无动画）
    lv_scr_load(screens[UI_SCREEN_MAIN]);
    current_screen = UI_SCREEN_MAIN;
    
    ESP_LOGI(TAG, "UI initialized with %d screens", UI_SCREEN_COUNT);
}

void ui_switch_screen(ui_screen_id_t screen_id)
{
    if (screen_id < 0 || screen_id >= UI_SCREEN_COUNT) return;
    if (screen_id == current_screen) return;
    
    // 直接切换界面，不使用动画避免花屏
    lv_scr_load(screens[screen_id]);
    current_screen = screen_id;
    
    ESP_LOGI(TAG, "Switched to screen %d", screen_id);
}

ui_screen_id_t ui_get_current_screen(void)
{
    return current_screen;
}

// 循环滚动：上一个界面
void ui_prev_screen(void)
{
    int new_screen = current_screen - 1;
    if (new_screen < 0) {
        new_screen = UI_SCREEN_COUNT - 1;
    }
    ui_switch_screen(new_screen);
}

// 循环滚动：下一个界面
void ui_next_screen(void)
{
    int new_screen = current_screen + 1;
    if (new_screen >= UI_SCREEN_COUNT) {
        new_screen = 0;
    }
    ui_switch_screen(new_screen);
}

void ui_update_time(void)
{
    if (time_label == NULL) return;
    
    struct timeval tv;
    struct tm *timeinfo;
    
    gettimeofday(&tv, NULL);
    timeinfo = localtime(&tv.tv_sec);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    lv_label_set_text(time_label, buf);
}
