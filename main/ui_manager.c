#include "ui_manager.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

static const char *TAG = "UI";

static lv_obj_t *screens[UI_SCREEN_COUNT];
static ui_screen_id_t current_screen = UI_SCREEN_MAIN;
static lv_obj_t *time_label = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *humidity_label = NULL;

// 大字体显示时间（HH:MM:SS格式）
lv_obj_t *ui_create_main_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_size(screen, 240, 240);
    
    // 顶部区域 - 温度和湿度（左右分布）
    // 温度标签 - 左上角
    temp_label = lv_label_create(screen);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFF6B6B), 0);  // 红色
    lv_label_set_text(temp_label, "25°C");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_14, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 10, 10);
    
    // 湿度标签 - 右上角
    humidity_label = lv_label_create(screen);
    lv_obj_set_style_text_color(humidity_label, lv_color_hex(0x4D96FF), 0);  // 蓝色
    lv_label_set_text(humidity_label, "65%");
    lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_14, 0);
    lv_obj_align(humidity_label, LV_ALIGN_TOP_RIGHT, -10, 10);
    
    // 中间大字体时间显示
    time_label = lv_label_create(screen);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(time_label, "12:00:00");
    // 使用放大效果模拟大字体
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(time_label, 4, 0);
    // 放大2.5倍
    lv_obj_set_style_transform_zoom(time_label, 250, 0);
    lv_obj_set_style_transform_pivot_x(time_label, 50, 0);  // 设置缩放点中心
    lv_obj_set_style_transform_pivot_y(time_label, 50, 0);
    lv_obj_center(time_label);
    
    // 底部提示
    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_label_set_text(hint, "Rotate to switch");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
    
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
    lv_list_add_btn(list, NULL, "Brightness");
    lv_list_add_btn(list, NULL, "Contrast");
    lv_list_add_btn(list, NULL, "Language");
    
    return screen;
}

void ui_init(void)
{
    // 创建所有界面（只有两个）
    screens[UI_SCREEN_MAIN] = ui_create_main_screen();
    screens[UI_SCREEN_SETTINGS] = ui_create_settings_screen();
    
    // 加载主界面
    lv_scr_load(screens[UI_SCREEN_MAIN]);
    current_screen = UI_SCREEN_MAIN;
    
    ESP_LOGI(TAG, "UI initialized: %d screens", UI_SCREEN_COUNT);
}

void ui_switch_screen(ui_screen_id_t screen_id)
{
    if (screen_id >= UI_SCREEN_COUNT) return;
    if (screen_id == current_screen) return;
    
    // 快速直接切换
    lv_scr_load(screens[screen_id]);
    current_screen = screen_id;
}

ui_screen_id_t ui_get_current_screen(void)
{
    return current_screen;
}

// 循环滚动：只有两个界面，直接切换
void ui_prev_screen(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

// 循环滚动：只有两个界面，直接切换
void ui_next_screen(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS);
}

void ui_update_time(void)
{
    if (time_label == NULL) return;
    
    struct timeval tv;
    struct tm *timeinfo;
    
    gettimeofday(&tv, NULL);
    timeinfo = localtime(&tv.tv_sec);
    
    char buf[16];
    // 显示时:分:秒
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", 
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    lv_label_set_text(time_label, buf);
}

void ui_update_temp(float temp)
{
    if (temp_label == NULL) return;
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°C", temp);
    lv_label_set_text(temp_label, buf);
}

void ui_update_humidity(float humidity)
{
    if (humidity_label == NULL) return;
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f%%", humidity);
    lv_label_set_text(humidity_label, buf);
}
