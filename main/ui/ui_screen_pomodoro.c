#include "ui_screen_pomodoro.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_POMODORO";

static lv_obj_t *progress_arc = NULL;
static lv_obj_t *timer_label = NULL;
static lv_obj_t *phase_label = NULL;
static lv_obj_t *completed_label = NULL;
static lv_obj_t *hint_label = NULL;
static lv_obj_t *cycle_label = NULL;
static lv_obj_t *total_time_label = NULL;

static uint32_t total_seconds = 25 * 60;

lv_obj_t* ui_screen_pomodoro_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    cycle_label = lv_label_create(screen);
    lv_obj_set_style_text_color(cycle_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(cycle_label, "Cycle: 1");
    lv_obj_set_style_text_font(cycle_label, &lv_font_montserrat_14, 0);
    lv_obj_align(cycle_label, LV_ALIGN_TOP_LEFT, 10, 8);

    phase_label = lv_label_create(screen);
    lv_obj_set_style_text_color(phase_label, lv_color_hex(0xFFAA00), 0);
    lv_label_set_text(phase_label, "WORK");
    lv_obj_set_style_text_font(phase_label, &lv_font_montserrat_14, 0);
    lv_obj_align(phase_label, LV_ALIGN_TOP_MID, 0, 8);

    completed_label = lv_label_create(screen);
    lv_obj_set_style_text_color(completed_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(completed_label, "0");
    lv_obj_set_style_text_font(completed_label, &lv_font_montserrat_14, 0);
    lv_obj_align(completed_label, LV_ALIGN_TOP_RIGHT, -10, 8);

    progress_arc = lv_arc_create(screen);
    lv_obj_set_size(progress_arc, 180, 180);
    lv_arc_set_rotation(progress_arc, 270);
    lv_arc_set_bg_angles(progress_arc, 0, 360);
    lv_arc_set_angles(progress_arc, 360, 0);
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0xFF6B6B), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(progress_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress_arc, 10, LV_PART_INDICATOR);
    lv_obj_remove_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(progress_arc, LV_ALIGN_CENTER, 0, 10);

    timer_label = lv_label_create(screen);
    lv_obj_set_style_text_color(timer_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(timer_label, "25:00");
    lv_obj_set_style_text_font(timer_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(timer_label, 1, 0);
    lv_obj_align(timer_label, LV_ALIGN_CENTER, 0, 10);

    total_time_label = lv_label_create(screen);
    lv_obj_set_style_text_color(total_time_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(total_time_label, "/ 25:00");
    lv_obj_set_style_text_font(total_time_label, &lv_font_montserrat_14, 0);
    lv_obj_align(total_time_label, LV_ALIGN_CENTER, 0, 28);

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, "Rotate: nav | SET: start/pause");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    ESP_LOGI(TAG, "Pomodoro screen created");
    return screen;
}

void ui_screen_pomodoro_update_time(uint32_t remaining_seconds)
{
    if (timer_label == NULL) return;
    
    uint32_t minutes = remaining_seconds / 60;
    uint32_t seconds = remaining_seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
    lv_label_set_text(timer_label, buf);
    
    if (progress_arc && total_seconds > 0) {
        uint32_t progress = (remaining_seconds * 360) / total_seconds;
        if (progress > 360) progress = 360;
        lv_arc_set_angles(progress_arc, progress, 0);
    }
    
    if (total_time_label) {
        uint32_t total_min = total_seconds / 60;
        uint32_t total_sec = total_seconds % 60;
        char total_buf[20];
        snprintf(total_buf, sizeof(total_buf), "/ %02lu:%02lu", (unsigned long)total_min, (unsigned long)total_sec);
        lv_label_set_text(total_time_label, total_buf);
    }
}

void ui_screen_pomodoro_update_phase(const char *phase)
{
    if (phase_label == NULL) return;
    lv_label_set_text(phase_label, phase);
}

void ui_screen_pomodoro_update_completed(uint32_t count)
{
    if (completed_label == NULL) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned int)count);
    lv_label_set_text(completed_label, buf);
}

void ui_screen_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed)
{
    ui_screen_pomodoro_update_time(remaining_seconds);
    ui_screen_pomodoro_update_completed(completed);
    
    if (phase_label == NULL) return;
    
    uint32_t color;
    const char *phase_text;
    switch (phase) {
        case 1:  // WORK
            color = 0xFF6B6B;
            phase_text = "WORK";
            total_seconds = 25 * 60;
            break;
        case 2:  // BREAK
            color = 0x4D96FF;
            phase_text = "BREAK";
            total_seconds = 5 * 60;
            break;
        case 3:  // LONG_BREAK
            color = 0x9B59B6;
            phase_text = "LONG BREAK";
            total_seconds = 15 * 60;
            break;
        case 4:  // PAUSED
            color = 0xFFFF00;
            phase_text = "PAUSED";
            break;
        default:  // IDLE
            color = 0xAAAAAA;
            phase_text = "IDLE";
            total_seconds = 25 * 60;
            break;
    }
    
    lv_obj_set_style_text_color(phase_label, lv_color_hex(color), 0);
    lv_label_set_text(phase_label, phase_text);
    
    if (cycle_label) {
        uint32_t cycle = (completed / 4) + 1;
        char cycle_buf[16];
        snprintf(cycle_buf, sizeof(cycle_buf), "Cycle: %lu", (unsigned long)cycle);
        lv_label_set_text(cycle_label, cycle_buf);
    }
    
    if (progress_arc) {
        lv_arc_set_angles(progress_arc, 360, 0);
        lv_obj_set_style_arc_color(progress_arc, lv_color_hex(color), LV_PART_INDICATOR);
    }
}
