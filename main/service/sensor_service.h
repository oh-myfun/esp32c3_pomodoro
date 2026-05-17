#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float altitude;
    bool temp_valid : 1;
    bool hum_valid : 1;
    bool press_valid : 1;
    bool alt_valid : 1;
} sensor_sample_t;

typedef struct {
    int16_t year;
    int8_t month;
    int8_t day;
    int8_t hour;
    int8_t minute;
    int8_t second;
} sensor_time_t;

typedef struct {
    int32_t temp_min, temp_max;  /* 0.1°C 单位 */
    int32_t press_min, press_max; /* hPa */
    int32_t alt_min, alt_max;    /* m */
} sensor_settings_t;

typedef enum {
    SENSOR_LEVEL_SECONDS = 0,  /* 60 points, 1s interval */
    SENSOR_LEVEL_MINUTES,      /* 60 points, 1min interval */
    SENSOR_LEVEL_HOURS,        /* 24 points, 1h interval */
    SENSOR_LEVEL_DAYS,         /* 30 points, 1d interval */
    SENSOR_LEVEL_COUNT
} sensor_level_t;

void sensor_service_init(void);
sensor_sample_t sensor_service_get_current(void);
int sensor_service_get_chart_data(sensor_level_t level, sensor_sample_t *buf, sensor_time_t *time_buf, int buf_size);
void sensor_service_get_settings(sensor_settings_t *out);
void sensor_service_set_settings(const sensor_settings_t *in);
void sensor_service_reset_settings(void);
