#pragma once

#include <stdint.h>

void backlight_init(void);
void backlight_set_brightness(uint8_t percent);
uint8_t backlight_get_brightness(void);
