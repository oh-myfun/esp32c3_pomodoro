#pragma once

#include <stdint.h>

void buzzer_init(void);
void buzzer_set_volume(uint8_t volume);
void buzzer_set_frequency(uint32_t freq_hz);
void buzzer_on(void);
void buzzer_off(void);
void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms);
