#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void backlight_init(void);
void backlight_set_brightness(uint8_t percent);
uint8_t backlight_get_brightness(void);

#ifdef __cplusplus
}
#endif
