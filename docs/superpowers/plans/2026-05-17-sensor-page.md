# 传感器页面实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增传感器数据页面，后台采集 AHT20+BMP280 数据，4 级聚合，LVGL 曲线图表展示，带时间轴和可配置归一化范围。

**Architecture:** sensor_service 作为独立 FreeRTOS 后台任务采集聚合，UI 层（ui_screen_sensor）读取数据并渲染图表。传感器设置页面（ui_screen_settings_sensor）提供 Y 轴范围调节。导航插入 MAIN→SENSOR→POMODORO。

**Tech Stack:** ESP-IDF v5.5.4, LVGL v9.5.0 (lv_chart), FreeRTOS, NVS, I2C (AHT20 + BMP280)

---

### Task 1: sensor_service.h — 类型定义与接口声明

**Files:**
- Create: `main/service/sensor_service.h`

- [ ] **Step 1: 创建 sensor_service.h**

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float altitude;
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
    int temp_min, temp_max;      /* 0.1°C 单位 */
    int press_min, press_max;    /* hPa */
    int alt_min, alt_max;        /* m */
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
```

- [ ] **Step 2: Commit**

```bash
git add main/service/sensor_service.h
git commit -m "feat: add sensor_service.h with types and API declarations"
```

---

### Task 2: sensor_service.c — 后台采集、聚合、设置管理

**Files:**
- Create: `main/service/sensor_service.c`

- [ ] **Step 1: 创建 sensor_service.c**

```c
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
#define DEF_TEMP_MIN   (-100)   /* -10.0°C */
#define DEF_TEMP_MAX   (500)    /*  50.0°C */
#define DEF_PRESS_MIN  (900)
#define DEF_PRESS_MAX  (1100)
#define DEF_ALT_MIN    (-100)
#define DEF_ALT_MAX    (3000)

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

static sensor_sample_t avg_samples(const sensor_sample_t *buf, int count)
{
    sensor_sample_t avg = {0};
    if (count <= 0) return avg;
    for (int i = 0; i < count; i++) {
        avg.temperature += buf[i].temperature;
        avg.humidity += buf[i].humidity;
        avg.pressure += buf[i].pressure;
        avg.altitude += buf[i].altitude;
    }
    avg.temperature /= count;
    avg.humidity /= count;
    avg.pressure /= count;
    avg.altitude /= count;
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

        sensor_sample_t sample = {0};
        if (ok_aht) {
            sample.temperature = temp;
            sample.humidity = hum;
        }
        if (ok_bmp) {
            sample.pressure = pressure;
            sample.altitude = 44330.0f * (1.0f - powf(pressure / 1013.25f, 0.1903f));
        }

        /* Get current time */
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);

        xSemaphoreTake(mutex, portMAX_DELAY);

        /* Write current sample */
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

    memset(&current_sample, 0, sizeof(current_sample));
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
    if (start < 0) start = 0;
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
```

- [ ] **Step 2: Commit**

```bash
git add main/service/sensor_service.c
git commit -m "feat: add sensor_service with background sampling and multi-level aggregation"
```

---

### Task 3: i18n 字符串 + storage NVS key

**Files:**
- Modify: `main/ui/i18n.h`
- Modify: `main/ui/i18n.c`
- Modify: `main/service/storage_service.h`

- [ ] **Step 1: 在 i18n.h 中添加传感器相关枚举值**

在 `STR_COUNT` 之前添加：

```c
    /* 在 STR_BACK (line ~158) 之前插入 */
    STR_T_SENSOR,          // "Sensor" / "传感器"
    STR_SENSOR_SETTINGS,   // "Sensor Settings" / "传感器设置"
    STR_TEMP_MIN,          // "Temp Min" / "温度最小"
    STR_TEMP_MAX,          // "Temp Max" / "温度最大"
    STR_PRESS_MIN,         // "Press Min" / "气压最小"
    STR_PRESS_MAX,         // "Press Max" / "气压最大"
    STR_ALT_MIN,           // "Alt Min" / "海拔最小"
    STR_ALT_MAX,           // "Alt Max" / "海拔最大"
    STR_FMT_TEMP_RANGE,    // "%.1f°C" / "%.1f°C"
    STR_FMT_PRESS_RANGE,   // "%dhPa" / "%dhPa"
    STR_FMT_ALT_RANGE,     // "%dm" / "%dm"
    STR_SEC_LEVEL,         // "Sec" / "秒级"
    STR_MIN_LEVEL,         // "Min" / "分级"
    STR_HOUR_LEVEL,        // "Hour" / "时级"
    STR_DAY_LEVEL,         // "Day" / "天级"
    STR_H_SENSOR_HINT,     // "SET:Level Press:Settings" / "SET:切换级别 按:设置"
    STR_H_SENSOR_EDIT,     // "SET:Save Encoder:Adjust" / "SET:保存 编码器:调节"
```

- [ ] **Step 2: 在 i18n.c 中添加对应字符串**

在 `strings` 数组中使用 designated initializer 添加：

```c
    [STR_T_SENSOR]         = {"Sensor",       "传感器"},
    [STR_SENSOR_SETTINGS]  = {"Sensor Settings", "传感器设置"},
    [STR_TEMP_MIN]         = {"Temp Min",     "温度最小"},
    [STR_TEMP_MAX]         = {"Temp Max",     "温度最大"},
    [STR_PRESS_MIN]        = {"Press Min",    "气压最小"},
    [STR_PRESS_MAX]        = {"Press Max",    "气压最大"},
    [STR_ALT_MIN]          = {"Alt Min",      "海拔最小"},
    [STR_ALT_MAX]          = {"Alt Max",      "海拔最大"},
    [STR_FMT_TEMP_RANGE]   = {"%.1f°C",       "%.1f°C"},
    [STR_FMT_PRESS_RANGE]  = {"%dhPa",        "%dhPa"},
    [STR_FMT_ALT_RANGE]    = {"%dm",          "%dm"},
    [STR_SEC_LEVEL]        = {"Sec",          "秒级"},
    [STR_MIN_LEVEL]        = {"Min",          "分级"},
    [STR_HOUR_LEVEL]       = {"Hour",         "时级"},
    [STR_DAY_LEVEL]        = {"Day",          "天级"},
    [STR_H_SENSOR_HINT]    = {"SET:Level Press:Set", "SET:切换 按:设置"},
    [STR_H_SENSOR_EDIT]    = {"SET:Save Encoder:Adj", "SET:保存 编码器:调节"},
```

- [ ] **Step 3: Commit**

```bash
git add main/ui/i18n.h main/ui/i18n.c
git commit -m "feat: add sensor i18n strings"
```

---

### Task 4: ui_manager — 添加枚举值、注册 lazy creator

**Files:**
- Modify: `main/ui/ui_manager.h`
- Modify: `main/ui/ui_manager.c`

- [ ] **Step 1: 在 ui_manager.h 枚举中添加 UI_SCREEN_SENSOR 和 UI_SCREEN_SETTINGS_SENSOR**

在 `UI_SCREEN_BRIDGE_SCAN` 之后、`UI_SCREEN_COUNT` 之前添加：

```c
    UI_SCREEN_BRIDGE_SCAN,
    UI_SCREEN_SENSOR,              /* ← 新增 */
    UI_SCREEN_SETTINGS_SENSOR,     /* ← 新增 */
    UI_SCREEN_COUNT
```

- [ ] **Step 2: 在 ui_manager.c 的 ui_init 中注册 lazy creator**

在 `lazy_creators[UI_SCREEN_BRIDGE_SCAN] = ui_screen_bridge_scan_create;` 之后添加：

```c
    lazy_creators[UI_SCREEN_SENSOR] = ui_screen_sensor_create;
    lazy_creators[UI_SCREEN_SETTINGS_SENSOR] = ui_screen_settings_sensor_create;
```

需要添加的头文件声明（在 ui_manager.c 顶部 extern 引用或 include）：

```c
#include "ui_screen_sensor.h"
#include "ui_screen_settings_sensor.h"
```

- [ ] **Step 3: 在 ui_manager.c 的 screen_is_disposable 中添加 sensor settings**

```c
static bool screen_is_disposable(ui_screen_id_t id)
{
    return id == UI_SCREEN_SETTINGS_POMODORO ||
           ...
           id == UI_SCREEN_BRIDGE_SCAN ||
           id == UI_SCREEN_SETTINGS_SENSOR;   /* ← 新增 */
}
```

注意：`UI_SCREEN_SENSOR` 不加入 disposable，它是常驻顶层页面。

- [ ] **Step 4: 在 ui_manager.c 的 is_top_level 中添加 SENSOR**

```c
static bool is_top_level(ui_screen_id_t id)
{
    return id == UI_SCREEN_MAIN || id == UI_SCREEN_POMODORO ||
           id == UI_SCREEN_BUDDY || id == UI_SCREEN_SETTINGS ||
           id == UI_SCREEN_SENSOR;   /* ← 新增 */
}
```

- [ ] **Step 5: Commit**

```bash
git add main/ui/ui_manager.h main/ui/ui_manager.c
git commit -m "feat: register SENSOR and SETTINGS_SENSOR screens in ui_manager"
```

---

### Task 5: ui_screen_sensor.c/h — 传感器页面（数值 + 图表 + 时间轴）

**Files:**
- Create: `main/ui/ui_screen_sensor.h`
- Create: `main/ui/ui_screen_sensor.c`

- [ ] **Step 1: 创建 ui_screen_sensor.h**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_sensor_create(void);
```

- [ ] **Step 2: 创建 ui_screen_sensor.c**

```c
#include "ui_screen_sensor.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "service/sensor_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>

static const char *TAG = "UI_SENSOR";

/* Chart series colors */
#define COLOR_TEMP    lv_color_hex(0xFF6B6B)
#define COLOR_HUM     lv_color_hex(0x4D96FF)
#define COLOR_PRESS   lv_color_hex(0xAA88FF)
#define COLOR_ALT     lv_color_hex(0x66CC66)

static sensor_level_t current_level = SENSOR_LEVEL_SECONDS;

/* UI objects */
static lv_obj_t *values_label = NULL;
static lv_obj_t *chart = NULL;
static lv_obj_t *time_left_label = NULL;
static lv_obj_t *time_right_label = NULL;
static lv_obj_t *hint_label = NULL;

static lv_chart_series_t *ser_temp = NULL;
static lv_chart_series_t *ser_hum = NULL;
static lv_chart_series_t *ser_press = NULL;
static lv_chart_series_t *ser_alt = NULL;

static const str_id_t level_names[SENSOR_LEVEL_COUNT] = {
    STR_SEC_LEVEL, STR_MIN_LEVEL, STR_HOUR_LEVEL, STR_DAY_LEVEL
};

static const int level_point_counts[SENSOR_LEVEL_COUNT] = {
    60, 60, 24, 30
};

static void format_time_label(sensor_level_t level, const sensor_time_t *t, char *buf, size_t len)
{
    switch (level) {
        case SENSOR_LEVEL_SECONDS:
            snprintf(buf, len, "%02d:%02d", t->minute, t->second);
            break;
        case SENSOR_LEVEL_MINUTES:
            snprintf(buf, len, "%02d:%02d", t->hour, t->minute);
            break;
        case SENSOR_LEVEL_HOURS:
            snprintf(buf, len, "%02d %02d", t->day, t->hour);
            break;
        case SENSOR_LEVEL_DAYS:
            snprintf(buf, len, "%02d/%02d", t->month, t->day);
            break;
        default:
            buf[0] = '\0';
            break;
    }
}

static int normalize(float val, float min_val, float max_val)
{
    if (max_val <= min_val) return 50;
    float norm = (val - min_val) / (max_val - min_val) * 100.0f;
    if (norm < 0) norm = 0;
    if (norm > 100) norm = 100;
    return (int)norm;
}

static void update_chart(void)
{
    int pt_count = level_point_counts[current_level];
    sensor_sample_t *data = malloc(pt_count * sizeof(sensor_sample_t));
    sensor_time_t *times = malloc(pt_count * sizeof(sensor_time_t));
    if (!data || !times) {
        ESP_LOGE(TAG, "Failed to allocate chart buffers");
        free(data);
        free(times);
        return;
    }

    int count = sensor_service_get_chart_data(current_level, data, times, pt_count);

    /* Update point count if different */
    lv_chart_set_point_count(chart, pt_count);

    /* Get settings for normalization */
    sensor_settings_t s;
    sensor_service_get_settings(&s);
    float t_min = s.temp_min / 10.0f, t_max = s.temp_max / 10.0f;
    float p_min = (float)s.press_min, p_max = (float)s.press_max;
    float a_min = (float)s.alt_min, a_max = (float)s.alt_max;

    /* Fill series data */
    for (int i = 0; i < count; i++) {
        int idx = (i < pt_count) ? i : pt_count - 1;
        lv_chart_set_series_value_by_id(chart, ser_temp, idx, normalize(data[i].temperature, t_min, t_max));
        lv_chart_set_series_value_by_id(chart, ser_hum, idx, normalize(data[i].humidity, 0, 100));
        lv_chart_set_series_value_by_id(chart, ser_press, idx, normalize(data[i].pressure, p_min, p_max));
        lv_chart_set_series_value_by_id(chart, ser_alt, idx, normalize(data[i].altitude, a_min, a_max));
    }
    /* Fill remaining with LV_CHART_POINT_NONE */
    for (int i = count; i < pt_count; i++) {
        lv_chart_set_series_value_by_id(chart, ser_temp, i, LV_CHART_POINT_NONE);
        lv_chart_set_series_value_by_id(chart, ser_hum, i, LV_CHART_POINT_NONE);
        lv_chart_set_series_value_by_id(chart, ser_press, i, LV_CHART_POINT_NONE);
        lv_chart_set_series_value_by_id(chart, ser_alt, i, LV_CHART_POINT_NONE);
    }

    lv_chart_refresh(chart);

    /* Update time axis labels */
    char buf[16];
    if (count > 0) {
        format_time_label(current_level, &times[0], buf, sizeof(buf));
        lv_label_set_text(time_left_label, buf);
        format_time_label(current_level, &times[count - 1], buf, sizeof(buf));
        lv_label_set_text(time_right_label, buf);
    } else {
        lv_label_set_text(time_left_label, "--");
        lv_label_set_text(time_right_label, "--");
    }

    free(data);
    free(times);
}

static void update_values(void)
{
    sensor_sample_t s = sensor_service_get_current();
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f°C %.0f%% %.0fhPa %.0fm",
             s.temperature, s.humidity, s.pressure, s.altitude);
    lv_label_set_text(values_label, buf);
}

static void update_hint(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "●%s  %s", i18n(level_names[current_level]), i18n(STR_H_SENSOR_HINT));
    lv_label_set_text(hint_label, buf);
}

static void sensor_on_encoder_cw(void)
{
    ui_switch_screen(UI_SCREEN_POMODORO);
}

static void sensor_on_encoder_ccw(void)
{
    ui_switch_screen(UI_SCREEN_MAIN);
}

static void sensor_on_encoder_press(void)
{
    ui_switch_screen(UI_SCREEN_SETTINGS_SENSOR);
}

static void sensor_on_settings_press(void)
{
    current_level = (current_level + 1) % SENSOR_LEVEL_COUNT;
    update_hint();
    update_chart();
}

static void sensor_on_encoder_long_press(void)
{
    ui_switch_screen(UI_SCREEN_MAIN);
}

lv_obj_t* ui_screen_sensor_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_size(screen, 240, 240);

    /* Top: current values */
    values_label = lv_label_create(screen);
    lv_obj_set_style_text_color(values_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(values_label, &custom_font_16, 0);
    lv_label_set_text(values_label, "--C --% --hPa --m");
    lv_obj_align(values_label, LV_ALIGN_TOP_LEFT, 5, 5);

    /* Time axis: left label */
    time_left_label = lv_label_create(screen);
    lv_obj_set_style_text_color(time_left_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(time_left_label, &custom_font_14, 0);
    lv_label_set_text(time_left_label, "--");
    lv_obj_align(time_left_label, LV_ALIGN_TOP_LEFT, 5, 25);

    /* Time axis: right label */
    time_right_label = lv_label_create(screen);
    lv_obj_set_style_text_color(time_right_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(time_right_label, &custom_font_14, 0);
    lv_label_set_text(time_right_label, "--");
    lv_obj_align(time_right_label, LV_ALIGN_TOP_RIGHT, -5, 25);

    /* Chart */
    chart = lv_chart_create(screen);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_size(chart, 230, 145);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 42);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 0, 0);

    /* Add series */
    ser_temp = lv_chart_add_series(chart, COLOR_TEMP, LV_CHART_AXIS_PRIMARY_Y);
    ser_hum = lv_chart_add_series(chart, COLOR_HUM, LV_CHART_AXIS_PRIMARY_Y);
    ser_press = lv_chart_add_series(chart, COLOR_PRESS, LV_CHART_AXIS_PRIMARY_Y);
    ser_alt = lv_chart_add_series(chart, COLOR_ALT, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_point_count(chart, 60);

    /* Bottom: hint */
    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = sensor_on_encoder_cw,
        .on_encoder_ccw = sensor_on_encoder_ccw,
        .on_encoder_press = sensor_on_encoder_press,
        .on_settings_press = sensor_on_settings_press,
        .on_encoder_long_press = sensor_on_encoder_long_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SENSOR, &cbs);

    current_level = SENSOR_LEVEL_SECONDS;
    update_hint();

    ESP_LOGI(TAG, "Sensor screen created");
    return screen;
}

/* Called from ui_update_task to refresh display */
void ui_screen_sensor_update(void)
{
    if (values_label == NULL) return;
    update_values();
    update_chart();
}
```

- [ ] **Step 3: 在 ui_screen_sensor.h 中添加 update 函数声明**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_sensor_create(void);
void ui_screen_sensor_update(void);
```

- [ ] **Step 4: Commit**

```bash
git add main/ui/ui_screen_sensor.h main/ui/ui_screen_sensor.c
git commit -m "feat: add sensor screen with chart and time axis"
```

---

### Task 6: ui_screen_settings_sensor.c/h — 传感器设置页面

**Files:**
- Create: `main/ui/ui_screen_settings_sensor.h`
- Create: `main/ui/ui_screen_settings_sensor.c`

- [ ] **Step 1: 创建 ui_screen_settings_sensor.h**

```c
#pragma once

#include "lvgl.h"

lv_obj_t* ui_screen_settings_sensor_create(void);
```

- [ ] **Step 2: 创建 ui_screen_settings_sensor.c**

```c
#include "ui_screen_settings_sensor.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "ui_list.h"
#include "service/sensor_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_SETTINGS_SENSOR";

#define SENSOR_ITEM_COUNT 7
/* 0: temp_min, 1: temp_max, 2: press_min, 3: press_max, 4: alt_min, 5: alt_max, 6: reset */

typedef enum {
    SENSOR_MODE_NAV = 0,
    SENSOR_MODE_ADJUST,
} sensor_edit_mode_t;

static sensor_edit_mode_t edit_mode = SENSOR_MODE_NAV;
static int selected_item = 0;
static sensor_settings_t settings;

static lv_obj_t *screen = NULL;
static lv_obj_t *list_widget = NULL;
static lv_obj_t *hint_label = NULL;

static char item_keys[SENSOR_ITEM_COUNT][20];
static char item_values[SENSOR_ITEM_COUNT][12];
static ui_list_item_t items[SENSOR_ITEM_COUNT];

/* Step values for each item */
static const int steps[SENSOR_ITEM_COUNT] = {5, 5, 10, 10, 50, 50, 0};

static void update_display(void)
{
    /* Item labels */
    snprintf(item_keys[0], sizeof(item_keys[0]), "%s", i18n(STR_TEMP_MIN));
    snprintf(item_values[0], sizeof(item_values[0]), "%.1f°C", settings.temp_min / 10.0f);

    snprintf(item_keys[1], sizeof(item_keys[1]), "%s", i18n(STR_TEMP_MAX));
    snprintf(item_values[1], sizeof(item_values[1]), "%.1f°C", settings.temp_max / 10.0f);

    snprintf(item_keys[2], sizeof(item_keys[2]), "%s", i18n(STR_PRESS_MIN));
    snprintf(item_values[2], sizeof(item_values[2]), "%dhPa", settings.press_min);

    snprintf(item_keys[3], sizeof(item_keys[3]), "%s", i18n(STR_PRESS_MAX));
    snprintf(item_values[3], sizeof(item_values[3]), "%dhPa", settings.press_max);

    snprintf(item_keys[4], sizeof(item_keys[4]), "%s", i18n(STR_ALT_MIN));
    snprintf(item_values[4], sizeof(item_values[4]), "%dm", settings.alt_min);

    snprintf(item_keys[5], sizeof(item_keys[5]), "%s", i18n(STR_ALT_MAX));
    snprintf(item_values[5], sizeof(item_values[5]), "%dm", settings.alt_max);

    snprintf(item_keys[6], sizeof(item_keys[6]), "%s", i18n(STR_RESET));
    snprintf(item_values[6], sizeof(item_values[6]), "⇨");

    for (int i = 0; i < SENSOR_ITEM_COUNT; i++) {
        items[i].key = item_keys[i];
        items[i].value = item_values[i];
    }

    if (list_widget) {
        lv_color_t color;
        if (edit_mode == SENSOR_MODE_ADJUST) {
            color = lv_color_hex(0xFFFF00);
        } else {
            color = lv_color_hex(0x00FF00);
        }
        ui_list_set_selected_color(list_widget, color);
        ui_list_set_items(list_widget, items, SENSOR_ITEM_COUNT);
        ui_list_set_selected(list_widget, selected_item);
    }

    if (hint_label) {
        if (edit_mode == SENSOR_MODE_ADJUST) {
            lv_label_set_text(hint_label, i18n(STR_H_SENSOR_EDIT));
        } else {
            lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
        }
    }
}

static void save_current_item(void)
{
    sensor_service_set_settings(&settings);
}

static void sensor_set_on_encoder_cw(void)
{
    if (edit_mode == SENSOR_MODE_NAV) {
        selected_item = (selected_item + 1) % SENSOR_ITEM_COUNT;
        update_display();
    } else if (edit_mode == SENSOR_MODE_ADJUST) {
        if (selected_item == 6) return; /* reset item */
        int *vals[] = {&settings.temp_min, &settings.temp_max,
                       &settings.press_min, &settings.press_max,
                       &settings.alt_min, &settings.alt_max};
        *vals[selected_item] += steps[selected_item];
        update_display();
    }
}

static void sensor_set_on_encoder_ccw(void)
{
    if (edit_mode == SENSOR_MODE_NAV) {
        selected_item = (selected_item - 1 + SENSOR_ITEM_COUNT) % SENSOR_ITEM_COUNT;
        update_display();
    } else if (edit_mode == SENSOR_MODE_ADJUST) {
        if (selected_item == 6) return;
        int *vals[] = {&settings.temp_min, &settings.temp_max,
                       &settings.press_min, &settings.press_max,
                       &settings.alt_min, &settings.alt_max};
        *vals[selected_item] -= steps[selected_item];
        update_display();
    }
}

static void sensor_set_on_encoder_press(void)
{
    if (edit_mode == SENSOR_MODE_ADJUST) {
        save_current_item();
        edit_mode = SENSOR_MODE_NAV;
        update_display();
    } else {
        ui_go_back();
    }
}

static void sensor_set_on_settings_press(void)
{
    if (edit_mode == SENSOR_MODE_NAV) {
        if (selected_item == 6) {
            /* Reset to defaults */
            sensor_service_reset_settings();
            sensor_service_get_settings(&settings);
            update_display();
        } else {
            edit_mode = SENSOR_MODE_ADJUST;
            update_display();
        }
    } else {
        save_current_item();
        edit_mode = SENSOR_MODE_NAV;
        update_display();
    }
}

static void sensor_set_on_encoder_long_press(void)
{
    if (edit_mode == SENSOR_MODE_ADJUST) {
        /* Discard changes, reload from service */
        sensor_service_get_settings(&settings);
        edit_mode = SENSOR_MODE_NAV;
        update_display();
    } else {
        ui_go_back();
    }
}

lv_obj_t* ui_screen_settings_sensor_create(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_size(screen, 240, 240);
    }
    list_widget = NULL;
    hint_label = NULL;

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, i18n(STR_SENSOR_SETTINGS));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &custom_font_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    list_widget = ui_list_create(screen, 220, 196, 10, 30);

    /* Load current settings */
    sensor_service_get_settings(&settings);
    edit_mode = SENSOR_MODE_NAV;
    selected_item = 0;
    update_display();

    hint_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(hint_label, i18n(STR_H_SET_EDIT_PRESS_BACK));
    lv_obj_set_style_text_font(hint_label, &custom_font_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    static const ui_input_callbacks_t cbs = {
        .on_encoder_cw = sensor_set_on_encoder_cw,
        .on_encoder_ccw = sensor_set_on_encoder_ccw,
        .on_encoder_press = sensor_set_on_encoder_press,
        .on_encoder_long_press = sensor_set_on_encoder_long_press,
        .on_settings_press = sensor_set_on_settings_press,
    };
    ui_register_input_callbacks(UI_SCREEN_SETTINGS_SENSOR, &cbs);

    ESP_LOGI(TAG, "Settings Sensor screen created");
    return screen;
}
```

- [ ] **Step 3: Commit**

```bash
git add main/ui/ui_screen_settings_sensor.h main/ui/ui_screen_settings_sensor.c
git commit -m "feat: add sensor settings page with min/max range adjustment"
```

---

### Task 7: 集成修改 — main.c, ui_screen_main.c, ui_screen_pomodoro.c, CMakeLists.txt

**Files:**
- Modify: `main/main.c`
- Modify: `main/ui/ui_screen_main.c`
- Modify: `main/ui/ui_screen_pomodoro.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 修改 main.c**

**1a.** 在 `#include` 区域添加：
```c
#include "service/sensor_service.h"
#include "ui/ui_screen_sensor.h"
```

**1b.** 在 `app_main()` 中，`bmp280_init();` 之后添加：
```c
    sensor_service_init();
```

**1c.** 删除 `ui_update_task` 中的传感器读取代码块（sensor reading every 2 seconds 整段，约 10 行）。包括 `last_sensor_tick` 变量声明。

**1d.** 在 `ui_update_task` 中添加传感器页面更新逻辑。在 `last_sensor_tick` 删除后，在 WiFi/Debug tick 逻辑之后添加：
```c
            /* Update sensor page */
            static int64_t last_chart_tick = 0;
            if (current_screen == UI_SCREEN_SENSOR && now - last_chart_tick >= 1000) {
                ui_screen_sensor_update();
                last_chart_tick = now;
            }
```

- [ ] **Step 2: 修改 ui_screen_main.c**

**2a.** 删除三个 label 变量和相关代码：
- 删除 `temp_label`、`humidity_label`、`pressure_label` 静态变量声明
- 删除 `ui_screen_main_create()` 中创建这三个 label 的代码块（temp_label, humidity_label, pressure_label 的创建和样式设置）
- 删除 `ui_screen_main_update_temp()`、`ui_screen_main_update_humidity()`、`ui_screen_main_update_pressure()` 三个函数
- 删除 `#include <math.h>`（不再需要 powf）

**2b.** 将 CW 导航从 POMODORO 改为 SENSOR：
```c
static void main_on_encoder_cw(void)
{
    ui_switch_screen(UI_SCREEN_SENSOR);
}

static void main_on_encoder_press(void)
{
    ui_switch_screen(UI_SCREEN_SENSOR);
}
```

- [ ] **Step 3: 修改 ui_screen_pomodoro.c**

将 CCW 导航从 MAIN 改为 SENSOR：
```c
static void pomo_on_encoder_ccw(void)
{
    if (pomo_is_running()) return;
    ui_switch_screen(UI_SCREEN_SENSOR);
}
```

- [ ] **Step 4: 修改 CMakeLists.txt**

在 `ui/ui_screen_bridge_scan.c` 之后添加三行：
```cmake
                          service/sensor_service.c
                          ui/ui_screen_sensor.c
                          ui/ui_screen_settings_sensor.c
```

- [ ] **Step 5: Commit**

```bash
git add main/main.c main/ui/ui_screen_main.c main/ui/ui_screen_pomodoro.c main/CMakeLists.txt
git commit -m "feat: integrate sensor service, update navigation and remove main screen sensor labels"
```

---

### Task 8: 编译验证

- [ ] **Step 1: 构建**

```bash
./build.sh
```

预期：编译成功，无错误。

- [ ] **Step 2: 修复编译问题（如有）**

常见问题检查：
- `ui_screen_main_update_temp/humidity/pressure` 被其他文件引用？搜索并清理声明
- `sensor_service.h` include 路径正确
- LVGL chart API 名称匹配 v9（`lv_chart_set_series_value_by_id` 而非 `lv_chart_set_value_by_id`）
- custom_font.h 已正确 include

- [ ] **Step 3: 烧录验证**

```bash
./build.sh flash monitor
```

验证项：
1. 串口日志显示 `Sensor service initialized`、`Sensor task started`
2. 主界面不再显示温湿度气压
3. 编码器 CW 进入传感器页面，显示实时数值和图表
4. SET 键切换秒/分/时/天级别
5. 编码器按下进入传感器设置页面
6. 调节温度/气压/海拔范围后保存，返回传感器页面图表变化
7. "恢复默认" 正常工作
8. 导航流畅：MAIN→SENSOR→POMODORO→BUDDY，反向正常
