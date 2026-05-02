#pragma once

#include <stdint.h>

#define WS2812_GPIO GPIO_NUM_8

int  ws2812_init(void);
void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b);
void ws2812_off(void);
