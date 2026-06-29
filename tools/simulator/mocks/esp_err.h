#pragma once
#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK          0
#define ESP_FAIL        -1
#define ESP_ERR_NO_MEM  0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x104
#define ESP_ERR_NVS_NOT_FOUND 0x1104

#define ESP_ERROR_CHECK(x) do { esp_err_t __err = (x); (void)__err; } while(0)

static inline const char *esp_err_to_name(esp_err_t e) {
    (void)e;
    return "ESP_OK";
}
