#include "ui_screen_sensor.h"
#include "i18n.h"
#include "custom_font.h"
#include "ui_manager.h"
#include "service/sensor_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

static const char *TAG = "UI_SENSOR";

/* Chart series colors */
#define COLOR_TEMP    lv_color_hex(0xFF6B6B)
#define COLOR_HUM     lv_color_hex(0x4D96FF)
#define COLOR_PRESS   lv_color_hex(0xAA88FF)
#define COLOR_ALT     lv_color_hex(0x66CC66)

static sensor_level_t current_level = SENSOR_LEVEL_SECONDS;

/* UI objects */
static lv_obj_t *temp_label = NULL;
static lv_obj_t *hum_label = NULL;
static lv_obj_t *press_label = NULL;
static lv_obj_t *alt_label = NULL;
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

    /* Fill series data — invalid fields → LV_CHART_POINT_NONE */
    for (int i = 0; i < count; i++) {
        lv_chart_set_series_value_by_id(chart, ser_temp, i,
            data[i].temp_valid ? normalize(data[i].temperature, t_min, t_max) : LV_CHART_POINT_NONE);
        lv_chart_set_series_value_by_id(chart, ser_hum, i,
            data[i].hum_valid ? normalize(data[i].humidity, 0, 100) : LV_CHART_POINT_NONE);
        lv_chart_set_series_value_by_id(chart, ser_press, i,
            data[i].press_valid ? normalize(data[i].pressure, p_min, p_max) : LV_CHART_POINT_NONE);
        lv_chart_set_series_value_by_id(chart, ser_alt, i,
            data[i].alt_valid ? normalize(data[i].altitude, a_min, a_max) : LV_CHART_POINT_NONE);
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

    if (temp_label) {
        if (s.temp_valid) {
            char buf[20];
            snprintf(buf, sizeof(buf), "\xE2\x97\x8F%.1fC", s.temperature);
            lv_label_set_text(temp_label, buf);
        } else {
            lv_label_set_text(temp_label, "");
        }
    }

    if (hum_label) {
        if (s.hum_valid) {
            char buf[20];
            snprintf(buf, sizeof(buf), "\xE2\x97\x8F%.0f%%", s.humidity);
            lv_label_set_text(hum_label, buf);
        } else {
            lv_label_set_text(hum_label, "");
        }
    }

    if (press_label) {
        if (s.press_valid) {
            char buf[20];
            snprintf(buf, sizeof(buf), "\xE2\x97\x8F%.0fhPa", s.pressure);
            lv_label_set_text(press_label, buf);
        } else {
            lv_label_set_text(press_label, "");
        }
    }

    if (alt_label) {
        if (s.alt_valid) {
            char buf[20];
            snprintf(buf, sizeof(buf), "\xE2\x97\x8F%.0fm", s.altitude);
            lv_label_set_text(alt_label, buf);
        } else {
            lv_label_set_text(alt_label, "");
        }
    }
}

static void update_hint(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %s", i18n(level_names[current_level]), i18n(STR_H_SENSOR_HINT));
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

    /* Top: individual value labels with colored dots */
    temp_label = lv_label_create(screen);
    lv_obj_set_style_text_color(temp_label, COLOR_TEMP, 0);
    lv_obj_set_style_text_font(temp_label, &custom_font_14, 0);
    lv_label_set_text(temp_label, "");
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 5, 5);

    hum_label = lv_label_create(screen);
    lv_obj_set_style_text_color(hum_label, COLOR_HUM, 0);
    lv_obj_set_style_text_font(hum_label, &custom_font_14, 0);
    lv_label_set_text(hum_label, "");
    lv_obj_align(hum_label, LV_ALIGN_TOP_RIGHT, -60, 5);

    press_label = lv_label_create(screen);
    lv_obj_set_style_text_color(press_label, COLOR_PRESS, 0);
    lv_obj_set_style_text_font(press_label, &custom_font_14, 0);
    lv_label_set_text(press_label, "");
    lv_obj_align(press_label, LV_ALIGN_TOP_LEFT, 5, 20);

    alt_label = lv_label_create(screen);
    lv_obj_set_style_text_color(alt_label, COLOR_ALT, 0);
    lv_obj_set_style_text_font(alt_label, &custom_font_14, 0);
    lv_label_set_text(alt_label, "");
    lv_obj_align(alt_label, LV_ALIGN_TOP_RIGHT, -5, 20);

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
    lv_obj_set_style_line_width(chart, 1, LV_PART_ITEMS);       /* 曲线宽度 */
    lv_obj_set_size(chart, 230, 145);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 42);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 0, 0);

    /* Hide default axis lines and point markers */
    lv_obj_set_style_line_width(chart, 0, LV_PART_MAIN);          /* 隐藏默认轴线 */
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);        /* 隐藏数据点圆圈 */

    /* Add series */
    ser_temp = lv_chart_add_series(chart, COLOR_TEMP, LV_CHART_AXIS_PRIMARY_Y);
    ser_hum = lv_chart_add_series(chart, COLOR_HUM, LV_CHART_AXIS_PRIMARY_Y);
    ser_press = lv_chart_add_series(chart, COLOR_PRESS, LV_CHART_AXIS_PRIMARY_Y);
    ser_alt = lv_chart_add_series(chart, COLOR_ALT, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_point_count(chart, 60);

    /* X axis line at bottom of chart */
    lv_obj_t *xaxis = lv_line_create(screen);
    static lv_point_precise_t xaxis_pts[] = {{5, 192}, {235, 192}};
    lv_line_set_points(xaxis, xaxis_pts, 2);
    lv_obj_set_style_line_color(xaxis, lv_color_hex(0x444444), 0);
    lv_obj_set_style_line_width(xaxis, 1, 0);

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

void ui_screen_sensor_update(void)
{
    if (temp_label == NULL) return;
    update_values();
    update_chart();
}
