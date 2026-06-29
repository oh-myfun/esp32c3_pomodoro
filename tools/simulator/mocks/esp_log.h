#pragma once
#include <stdio.h>
#include <stdarg.h>

#ifndef LOG_TAG
#define LOG_TAG "SIM"
#endif

/* ESP-IDF log callback type */
typedef int (*vprintf_like_t)(const char *, va_list);

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stdout, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)
#define ESP_LOGV(tag, fmt, ...)

#define ESP_LOG_LEVEL_LOCAL(level, tag, fmt, ...)

static inline vprintf_like_t esp_log_set_vprintf(vprintf_like_t fn) {
    (void)fn;
    return NULL;
}

#define ESP_LOG_NONE    0
#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5
