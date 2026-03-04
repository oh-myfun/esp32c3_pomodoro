#pragma once

#include <stdint.h>
#include <stdbool.h>

#define POMODORO_DEFAULT_WORK_MINUTES   25
#define POMODORO_DEFAULT_BREAK_MINUTES   5
#define POMODORO_DEFAULT_LONG_BREAK_MINUTES  15
#define POMODORO_DEFAULT_CYCLES         4

typedef enum {
    POMODORO_PHASE_IDLE = 0,
    POMODORO_PHASE_WORK,
    POMODORO_PHASE_BREAK,
    POMODORO_PHASE_LONG_BREAK,
    POMODORO_PHASE_PAUSED
} pomodoro_phase_t;

typedef struct {
    uint16_t work_minutes;
    uint16_t break_minutes;
    uint16_t long_break_minutes;
    uint16_t cycles_until_long_break;
} pomodoro_settings_t;

typedef struct {
    pomodoro_phase_t phase;
    uint32_t remaining_seconds;
    uint32_t completed_count;
    uint16_t current_cycle;
    uint32_t last_start_timestamp;
    bool is_paused;
} pomodoro_state_t;

void pomodoro_engine_init(void);

void pomodoro_engine_start(void);

void pomodoro_engine_pause(void);

void pomodoro_engine_resume(void);

void pomodoro_engine_stop(void);

void pomodoro_engine_reset(void);

void pomodoro_engine_tick(void);

pomodoro_state_t pomodoro_engine_get_state(void);

pomodoro_settings_t pomodoro_engine_get_settings(void);

void pomodoro_engine_set_work_minutes(uint16_t minutes);

void pomodoro_engine_set_break_minutes(uint16_t minutes);

void pomodoro_engine_set_long_break_minutes(uint16_t minutes);

void pomodoro_engine_set_cycles(uint16_t cycles);

void pomodoro_engine_clear_completed_count(void);

void pomodoro_engine_save_state(void);

void pomodoro_engine_load_state(void);
