#pragma once

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

lv_obj_t* ui_screen_pomodoro_create(void);
void ui_screen_pomodoro_update_state(uint8_t phase, uint32_t remaining_seconds, uint32_t completed, uint16_t current_cycle);
void ui_screen_pomodoro_timer_tick(void);
void ui_screen_pomodoro_refresh(void);

/* Timer (countdown alarm) access for deep-sleep scheduling/compensation. */
bool ui_screen_pomodoro_timer_is_running(void);
uint32_t ui_screen_pomodoro_timer_get_remaining(void);

/* Hooks for deep-sleep entry/exit so the countdown timer stays accurate
 * across light sleep (same pattern as pomodoro_engine). */
void ui_screen_pomodoro_timer_on_deep_sleep_enter(void);
void ui_screen_pomodoro_timer_on_deep_sleep_exit(void);
