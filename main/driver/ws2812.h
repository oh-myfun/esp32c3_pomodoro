#pragma once

#include <stdint.h>
#include "driver/gpio.h"

#define WS2812_GPIO GPIO_NUM_8

typedef struct {
    uint8_t r, g, b;
} rgb_t;

int  ws2812_init(void);
void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b);
void ws2812_set_pixels(const rgb_t *pixels, uint8_t count);
void ws2812_off(void);
