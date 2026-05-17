#include "sensor_service.h"
#include "driver/aht20.h"
#include "driver/bmp280.h"
#include "service/storage_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <time.h>
#include <math.h>
#include <string.h>

static const char *TAG = "SENSOR_SVC";

/* Ring buffer sizes per level */
#define SEC_COUNT   60
#define MIN_COUNT   60
#define HOUR_COUNT  24
#define DAY_COUNT   30

/* Ring buffers */
static sensor_sample_t seconds_buf[SEC_COUNT];
static sensor_sample_t minutes_buf[MIN_COUNT];
static sensor_sample_t hours_buf[HOUR_COUNT];
static sensor_sample_t days_buf[DAY_COUNT];

static sensor_time_t seconds_time[SEC_COUNT];
static sensor_time_t minutes_time[MIN_COUNT];
static sensor_time_t hours_time[HOUR_COUNT];
static sensor_time_t days_time[DAY_COUNT];

static int sec_pos = 0, sec_count = 0;
static int min_pos = 0, min_count = 0;
static int hour_pos = 0, hour_count = 0;
static int day_pos = 0, day_count = 0;

static sensor_sample_t current_sample;
static sensor_settings_t settings;
static SemaphoreHandle_t mutex = NULL;
static bool running = false;

/* Default settings */
#define DEF_TEMP_MIN   ((int32_t)-100)   /* -10.0°C */
#define DEF_TEMP_MAX   ((int32_t)500)    /*  50.0°C */
#define DEF_PRESS_MIN  ((int32_t)900)
#define DEF_PRESS_MAX  ((int32_t)1100)
#define DEF_ALT_MIN    ((int32_t)-100)
#define DEF_ALT_MAX    ((int32_t)3000)

/* NVS keys */
#define KEY_S_TEMP_MIN   "s_temp_min"
#define KEY_S_TEMP_MAX   "s_temp_max"
#define KEY_S_PRESS_MIN  "s_press_min"
#define KEY_S_PRESS_MAX  "s_press_max"
#define KEY_S_ALT_MIN    "s_alt_min"
#define KEY_S_ALT_MAX    "s_alt_max"

static void load_settings(void)
{
    if (!storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_S_TEMP_MIN, &settings.temp_min))
        settings.temp_min = DEF_TEMP_MIN;
    if (!storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_S_TEMP_MAX, &settings.temp_max))
        settings.temp_max = DEF_TEMP_MAX;
    if (!storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_S_PRESS_MIN, &settings.press_min))
        settings.press_min = DEF_PRESS_MIN;
    if (!storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_S_PRESS_MAX, &settings.press_max))
        settings.press_max = DEF_PRESS_MAX;
    if (!storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_S_ALT_MIN, &settings.alt_min))
        settings.alt_min = DEF_ALT_MIN;
    if (!storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_S_ALT_MAX, &settings.alt_max))
        settings.alt_max = DEF_ALT_MAX;
}

static void save_setting(const char *key, int32_t val)
{
    storage_save_int(STORAGE_NAMESPACE_SETTINGS, key, val);
}

static void time_to_sensor_time(const struct tm *t, sensor_time_t *st)
{
    st->year = t->tm_year + 1900;
    st->month = t->tm_mon + 1;
    st->day = t->tm_mday;
    st->hour = t->tm_hour;
    st->minute = t->tm_min;
    st->second = t->tm_sec;
}

static sensor_sample_t no_data_sample(void)
{
    sensor_sample_t s = {SENSOR_NO_DATA, SENSOR_NO_DATA, SENSOR_NO_DATA, SENSOR_NO_DATA};
    return s;
}

static bool is_valid(float val)
{
    return val != SENSOR_NO_DATA;
}

static sensor_sample_t avg_samples(const sensor_sample_t *buf, int count)
{
    sensor_sample_t avg = no_data_sample();
    if (count <= 0) return avg;

    float t_sum = 0, h_sum = 0, p_sum = 0, a_sum = 0;
    int t_cnt = 0, h_cnt = 0, p_cnt = 0, a_cnt = 0;

    for (int i = 0; i < count; i++) {
        if (is_valid(buf[i].temperature)) { t_sum += buf[i].temperature; t_cnt++; }
        if (is_valid(buf[i].humidity))    { h_sum += buf[i].humidity;    h_cnt++; }
        if (is_valid(buf[i].pressure))    { p_sum += buf[i].pressure;    p_cnt++; }
        if (is_valid(buf[i].altitude))    { a_sum += buf[i].altitude;    a_cnt++; }
    }

    if (t_cnt) avg.temperature = t_sum / t_cnt;
    if (h_cnt) avg.humidity    = h_sum / h_cnt;
    if (p_cnt) avg.pressure    = p_sum / p_cnt;
    if (a_cnt) avg.altitude    = a_sum / a_cnt;

    return avg;
}

static void aggregate_minute(const struct tm *t)
{
    if (sec_count == 0) return;
    int count = sec_count < SEC_COUNT ? sec_count : SEC_COUNT;
    minutes_buf[min_pos % MIN_COUNT] = avg_samples(seconds_buf, count);
    time_to_sensor_time(t, &minutes_time[min_pos % MIN_COUNT]);
    min_pos++;
    if (min_count < MIN_COUNT) min_count++;
}

static void aggregate_hour(const struct tm *t)
{
    if (min_count == 0) return;
    int count = min_count < MIN_COUNT ? min_count : MIN_COUNT;
    hours_buf[hour_pos % HOUR_COUNT] = avg_samples(minutes_buf, count);
    time_to_sensor_time(t, &hours_time[hour_pos % HOUR_COUNT]);
    hour_pos++;
    if (hour_count < HOUR_COUNT) hour_count++;
}

static void aggregate_day(const struct tm *t)
{
    if (hour_count == 0) return;
    int count = hour_count < HOUR_COUNT ? hour_count : HOUR_COUNT;
    days_buf[day_pos % DAY_COUNT] = avg_samples(hours_buf, count);
    time_to_sensor_time(t, &days_time[day_pos % DAY_COUNT]);
    day_pos++;
    if (day_count < DAY_COUNT) day_count++;
}

static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor task started");
    int last_sec = -1, last_min = -1, last_hour = -1;

    while (running) {
        /* Read sensors */
        float temp = 0, hum = 0, pressure = 0;
        float bmp_temp = 0;
        bool ok_aht = aht20_read(&temp, &hum);
        bool ok_bmp = bmp280_read(&bmp_temp, &pressure);

        sensor_sample_t sample = no_data_sample();
        if (ok_aht) {
            sample.temperature = temp;
            sample.humidity = hum;
        }
        if (ok_bmp) {
            sample.pressure = pressure;
            sample.altitude = 44330.0f * (1.0f - powf(pressure / 1013.25f, 0.1903f));
        }

        /* Skip if both sensors failed */
        if (!ok_aht && !ok_bmp) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Get current time */
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);

        xSemaphoreTake(mutex, portMAX_DELAY);

        /* Update current sample */
        current_sample = sample;

        /* Write to seconds buffer */
        seconds_buf[sec_pos % SEC_COUNT] = sample;
        time_to_sensor_time(&t, &seconds_time[sec_pos % SEC_COUNT]);
        sec_pos++;
        if (sec_count < SEC_COUNT) sec_count++;

        /* Aggregation at time boundaries */
        if (t.tm_sec == 0 && t.tm_sec != last_sec) {
            aggregate_minute(&t);
        }
        if (t.tm_sec == 0 && t.tm_min == 0 && (t.tm_sec != last_sec || t.tm_min != last_min)) {
            aggregate_hour(&t);
        }
        if (t.tm_sec == 0 && t.tm_min == 0 && t.tm_hour == 0 &&
            (t.tm_sec != last_sec || t.tm_min != last_min || t.tm_hour != last_hour)) {
            aggregate_day(&t);
        }

        last_sec = t.tm_sec;
        last_min = t.tm_min;
        last_hour = t.tm_hour;

        xSemaphoreGive(mutex);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

void sensor_service_init(void)
{
    mutex = xSemaphoreCreateMutex();
    load_settings();

    current_sample = no_data_sample();
    running = true;

    xTaskCreate(sensor_task, "SensorSvc", 4096, NULL, 1, NULL);
    ESP_LOGI(TAG, "Sensor service initialized");
}

sensor_sample_t sensor_service_get_current(void)
{
    sensor_sample_t sample;
    xSemaphoreTake(mutex, portMAX_DELAY);
    sample = current_sample;
    xSemaphoreGive(mutex);
    return sample;
}

int sensor_service_get_chart_data(sensor_level_t level, sensor_sample_t *buf, sensor_time_t *time_buf, int buf_size)
{
    xSemaphoreTake(mutex, portMAX_DELAY);

    const sensor_sample_t *src_buf = NULL;
    const sensor_time_t *src_time = NULL;
    int count = 0, pos = 0, max = 0;

    switch (level) {
        case SENSOR_LEVEL_SECONDS:
            src_buf = seconds_buf; src_time = seconds_time;
            count = sec_count; pos = sec_pos; max = SEC_COUNT;
            break;
        case SENSOR_LEVEL_MINUTES:
            src_buf = minutes_buf; src_time = minutes_time;
            count = min_count; pos = min_pos; max = MIN_COUNT;
            break;
        case SENSOR_LEVEL_HOURS:
            src_buf = hours_buf; src_time = hours_time;
            count = hour_count; pos = hour_pos; max = HOUR_COUNT;
            break;
        case SENSOR_LEVEL_DAYS:
            src_buf = days_buf; src_time = days_time;
            count = day_count; pos = day_pos; max = DAY_COUNT;
            break;
        default:
            xSemaphoreGive(mutex);
            return 0;
    }

    int to_copy = count < buf_size ? count : buf_size;
    /* Read from oldest to newest (ring buffer order) */
    int start = (pos - to_copy + max) % max;
    for (int i = 0; i < to_copy; i++) {
        int idx = (start + i) % max;
        buf[i] = src_buf[idx];
        if (time_buf) time_buf[i] = src_time[idx];
    }

    xSemaphoreGive(mutex);
    return to_copy;
}

void sensor_service_get_settings(sensor_settings_t *out)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    *out = settings;
    xSemaphoreGive(mutex);
}

void sensor_service_set_settings(const sensor_settings_t *in)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    settings = *in;
    xSemaphoreGive(mutex);

    save_setting(KEY_S_TEMP_MIN, in->temp_min);
    save_setting(KEY_S_TEMP_MAX, in->temp_max);
    save_setting(KEY_S_PRESS_MIN, in->press_min);
    save_setting(KEY_S_PRESS_MAX, in->press_max);
    save_setting(KEY_S_ALT_MIN, in->alt_min);
    save_setting(KEY_S_ALT_MAX, in->alt_max);
}

void sensor_service_reset_settings(void)
{
    sensor_settings_t defaults = {
        .temp_min = DEF_TEMP_MIN, .temp_max = DEF_TEMP_MAX,
        .press_min = DEF_PRESS_MIN, .press_max = DEF_PRESS_MAX,
        .alt_min = DEF_ALT_MIN, .alt_max = DEF_ALT_MAX,
    };
    sensor_service_set_settings(&defaults);
}
