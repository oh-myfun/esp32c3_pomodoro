#include "pomodoro_engine.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "storage/storage_service.h"
#include <string.h>

static const char *TAG = "POMODORO";

static pomodoro_state_t current_state = {
    .phase = POMODORO_PHASE_IDLE,
    .remaining_seconds = 0,
    .completed_count = 0,
    .current_cycle = 0,
    .last_start_timestamp = 0,
    .is_paused = false
};

static pomodoro_settings_t settings = {
    .work_minutes = POMODORO_DEFAULT_WORK_MINUTES,
    .break_minutes = POMODORO_DEFAULT_BREAK_MINUTES,
    .long_break_minutes = POMODORO_DEFAULT_LONG_BREAK_MINUTES,
    .cycles_until_long_break = POMODORO_DEFAULT_CYCLES
};

void pomodoro_engine_init(void)
{
    pomodoro_engine_load_state();
    ESP_LOGI(TAG, "Pomodoro engine initialized: work=%d min, break=%d min, completed=%lu",
             settings.work_minutes, settings.break_minutes, current_state.completed_count);
}

void pomodoro_engine_start(void)
{
    if (current_state.phase == POMODORO_PHASE_IDLE || 
        current_state.phase == POMODORO_PHASE_WORK) {
        current_state.phase = POMODORO_PHASE_WORK;
        current_state.remaining_seconds = settings.work_minutes * 60;
        current_state.is_paused = false;
        current_state.last_start_timestamp = esp_timer_get_time() / 1000000;
        ESP_LOGI(TAG, "Started WORK phase: %lu seconds", current_state.remaining_seconds);
    }
}

void pomodoro_engine_pause(void)
{
    if (current_state.phase == POMODORO_PHASE_WORK || 
        current_state.phase == POMODORO_PHASE_BREAK ||
        current_state.phase == POMODORO_PHASE_LONG_BREAK) {
        current_state.phase = POMODORO_PHASE_PAUSED;
        current_state.is_paused = true;
        ESP_LOGI(TAG, "Paused");
    }
}

void pomodoro_engine_resume(void)
{
    if (current_state.phase == POMODORO_PHASE_PAUSED) {
        current_state.is_paused = false;
        current_state.phase = POMODORO_PHASE_WORK;
        ESP_LOGI(TAG, "Resumed");
    }
}

void pomodoro_engine_stop(void)
{
    current_state.phase = POMODORO_PHASE_IDLE;
    current_state.remaining_seconds = 0;
    current_state.current_cycle = 0;
    current_state.is_paused = false;
    ESP_LOGI(TAG, "Stopped");
}

void pomodoro_engine_reset(void)
{
    current_state.phase = POMODORO_PHASE_IDLE;
    current_state.remaining_seconds = 0;
    current_state.completed_count = 0;
    current_state.current_cycle = 0;
    current_state.is_paused = false;
    pomodoro_engine_save_state();
    ESP_LOGI(TAG, "Reset");
}

void pomodoro_engine_tick(void)
{
    if (current_state.phase == POMODORO_PHASE_IDLE || 
        current_state.phase == POMODORO_PHASE_PAUSED) {
        return;
    }

    if (current_state.remaining_seconds > 0) {
        current_state.remaining_seconds--;
    } else {
        if (current_state.phase == POMODORO_PHASE_WORK) {
            current_state.completed_count++;
            current_state.current_cycle++;
            
            if (current_state.current_cycle >= settings.cycles_until_long_break) {
                current_state.phase = POMODORO_PHASE_LONG_BREAK;
                current_state.remaining_seconds = settings.long_break_minutes * 60;
                current_state.current_cycle = 0;
                ESP_LOGI(TAG, "Long break started, completed: %lu", current_state.completed_count);
            } else {
                current_state.phase = POMODORO_PHASE_BREAK;
                current_state.remaining_seconds = settings.break_minutes * 60;
                ESP_LOGI(TAG, "Break started, completed: %lu", current_state.completed_count);
            }
            pomodoro_engine_save_state();
        } else if (current_state.phase == POMODORO_PHASE_BREAK || 
                   current_state.phase == POMODORO_PHASE_LONG_BREAK) {
            current_state.phase = POMODORO_PHASE_WORK;
            current_state.remaining_seconds = settings.work_minutes * 60;
            ESP_LOGI(TAG, "Work phase started");
        }
    }
}

pomodoro_state_t pomodoro_engine_get_state(void)
{
    return current_state;
}

pomodoro_settings_t pomodoro_engine_get_settings(void)
{
    return settings;
}

void pomodoro_engine_set_work_minutes(uint16_t minutes)
{
    if (minutes >= 1 && minutes <= 60) {
        settings.work_minutes = minutes;
        int32_t data[4] = {settings.work_minutes, settings.break_minutes, settings.long_break_minutes, settings.cycles_until_long_break};
        storage_save_pomodoro_settings(data);
        ESP_LOGI(TAG, "Work minutes set to %d", minutes);
    }
}

void pomodoro_engine_set_break_minutes(uint16_t minutes)
{
    if (minutes >= 1 && minutes <= 30) {
        settings.break_minutes = minutes;
        int32_t data[4] = {settings.work_minutes, settings.break_minutes, settings.long_break_minutes, settings.cycles_until_long_break};
        storage_save_pomodoro_settings(data);
        ESP_LOGI(TAG, "Break minutes set to %d", minutes);
    }
}

void pomodoro_engine_set_long_break_minutes(uint16_t minutes)
{
    if (minutes >= 1 && minutes <= 60) {
        settings.long_break_minutes = minutes;
        int32_t data[4] = {settings.work_minutes, settings.break_minutes, settings.long_break_minutes, settings.cycles_until_long_break};
        storage_save_pomodoro_settings(data);
        ESP_LOGI(TAG, "Long break minutes set to %d", minutes);
    }
}

void pomodoro_engine_set_cycles(uint16_t cycles)
{
    if (cycles >= 1 && cycles <= 10) {
        settings.cycles_until_long_break = cycles;
        int32_t data[4] = {settings.work_minutes, settings.break_minutes, settings.long_break_minutes, settings.cycles_until_long_break};
        storage_save_pomodoro_settings(data);
        ESP_LOGI(TAG, "Cycles set to %d", cycles);
    }
}

void pomodoro_engine_clear_completed_count(void)
{
    current_state.completed_count = 0;
    current_state.current_cycle = 0;
    pomodoro_engine_save_state();
    ESP_LOGI(TAG, "Completed count cleared");
}

void pomodoro_engine_save_state(void)
{
    int32_t data[2] = {current_state.completed_count, current_state.current_cycle};
    storage_save_pomodoro_state(data);
}

void pomodoro_engine_load_state(void)
{
    int32_t state_data[2] = {0, 0};
    int32_t settings_data[4] = {25, 5, 15, 4};
    
    storage_load_pomodoro_state(state_data);
    storage_load_pomodoro_settings(settings_data);
    
    current_state.completed_count = state_data[0];
    current_state.current_cycle = state_data[1];
    settings.work_minutes = settings_data[0];
    settings.break_minutes = settings_data[1];
    settings.long_break_minutes = settings_data[2];
    settings.cycles_until_long_break = settings_data[3];
}
