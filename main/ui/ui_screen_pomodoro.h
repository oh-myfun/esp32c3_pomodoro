#pragma once

#include "lvgl.h"
#include <stdint.h>

lv_obj_t* ui_screen_pomodoro_create(void);
void ui_screen_pomodoro_update_time(uint32_t remaining_seconds);
void ui_screen_pomodoro_update_phase(const char *phase);
void ui_screen_pomodoro_update_completed(uint32_t count);
void ui_screen_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed, uint16_t current_cycle);
void ui_screen_pomodoro_refresh(void);
