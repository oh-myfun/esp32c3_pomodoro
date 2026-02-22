#include "ui_screen_pomodoro.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_POMODORO";

static lv_obj_t *timer_label = NULL;
static lv_obj_t *phase_label = NULL;
static lv_obj_t *completed_label = NULL;
static lv_obj_t *hint_label = NULL;

lv_obj_t* ui_screen_pomodoro_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_size(screen, 240, 240);

    phase_label = lv_label_create(screen);
    lv_obj_set_style_text_color(phase_label, lv_color_hex(0xFFAA00), 0);
    lv_label_set_text(phase_label, "WORK");
    lv_obj_set_style_text_font(phase_label, &lv_font_montserrat_14, 0);
    lv_obj_align(phase_label, LV_ALIGN_TOP_MID, 0, 20);

    timer_label = lv_label_create(screen);
    lv_obj_set_style_text_color(timer_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(timer_label, "25:00");
    lv_obj_set_style_text_font(timer_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_letter_space(timer_label, 2, 0);
    lv_obj_align(timer_label, LV_ALIGN_CENTER, 0, -10);

    completed_label = lv_label_create(screen);
    lv_obj_set_style_text_color(completed_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(completed_label, "Completed: 0");
    lv_obj_set_style_text_font(completed_label, &lv_font_montserrat_14, 0);
    lv_obj_align(completed_label, LV_ALIGN_CENTER, 0, 50);

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
}

void ui_screen_pomodoro_update_phase(const char *phase)
{
    if (phase_label == NULL) return;
    lv_label_set_text(phase_label, phase);
}

void ui_screen_pomodoro_update_completed(uint32_t count)
{
    if (completed_label == NULL) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "Completed: %u", (unsigned int)count);
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
            break;
        case 2:  // BREAK
            color = 0x4D96FF;
            phase_text = "BREAK";
            break;
        case 3:  // LONG_BREAK
            color = 0x9B59B6;
            phase_text = "LONG BREAK";
            break;
        case 4:  // PAUSED
            color = 0xFFFF00;
            phase_text = "PAUSED";
            break;
        default:  // IDLE
            color = 0xAAAAAA;
            phase_text = "IDLE";
            break;
    }
    
    lv_obj_set_style_text_color(phase_label, lv_color_hex(color), 0);
    lv_label_set_text(phase_label, phase_text);
}
