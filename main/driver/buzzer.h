#pragma once

#include <stdint.h>
#include <stdbool.h>

void buzzer_init(void);

typedef struct {
    uint32_t freq_hz;    // 0 = rest (silence)
    uint16_t duration_ms;
} buzzer_note_t;

// Non-blocking melody playback using esp_timer
void buzzer_play_melody(const buzzer_note_t *notes, uint8_t count);
void buzzer_stop(void);
bool buzzer_is_playing(void);
