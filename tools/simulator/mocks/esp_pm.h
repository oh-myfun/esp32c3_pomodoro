#pragma once
#include "esp_err.h"

typedef struct {
    int max_freq_mhz;
    int min_freq_mhz;
    bool light_sleep_enable;
} esp_pm_config_t;

typedef void *esp_pm_lock_handle_t;

#define ESP_PM_CPU_FREQ_MAX    0x01
#define ESP_PM_APB_FREQ_MAX    0x02
#define ESP_PM_NO_LIGHT_SLEEP  0x04

static inline esp_err_t esp_pm_configure(const void *config) { (void)config; return ESP_OK; }
static inline esp_err_t esp_pm_lock_create(int type, int wakeup_opt,
                                            const char *name, esp_pm_lock_handle_t *out) {
    (void)type; (void)wakeup_opt; (void)name;
    static int dummy;
    if (out) *out = &dummy;
    return ESP_OK;
}
static inline esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t h) { (void)h; return ESP_OK; }
