#pragma once

#include <stdint.h>
#include <stdbool.h>

void bmp280_init(void);
bool bmp280_read(float *temperature, float *pressure_hpa);
bool bmp280_is_available(void);
