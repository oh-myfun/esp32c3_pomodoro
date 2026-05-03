#pragma once

#include <stdint.h>
#include <stdbool.h>

void buzzer_init(void);
void buzzer_set_volume(uint8_t volume);
void buzzer_set_frequency(uint32_t freq_hz);
void buzzer_on(void);
void buzzer_off(void);
void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms);

typedef struct {
    uint32_t freq_hz;    // 0 = rest (silence)
    uint16_t duration_ms;
} buzzer_note_t;

// Non-blocking melody playback using esp_timer
void buzzer_play_melody(const buzzer_note_t *notes, uint8_t count);
void buzzer_stop(void);
bool buzzer_is_playing(void);
