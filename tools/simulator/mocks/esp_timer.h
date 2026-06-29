#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*esp_timer_cb_t)(void *arg);

typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    const char *name;
    int dispatch_method;
    int skip_unhandled_events;
} esp_timer_create_args_t;

typedef struct esp_timer *esp_timer_handle_t;

int64_t esp_timer_get_time(void);

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args,
                           esp_timer_handle_t *out_handle);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);

void esp_timer_impl_advance(int64_t us);

#ifdef __cplusplus
}
#endif
