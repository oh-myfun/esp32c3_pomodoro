#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

void aht20_init(void);
bool aht20_read(float *temperature, float *humidity);
i2c_master_bus_handle_t aht20_get_i2c_bus(void);
