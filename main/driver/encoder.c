#include "encoder.h"
#include "esp_log.h"
#include "esp_err.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "button_gpio.h"

static const char *TAG = "ENCODER";

static knob_handle_t g_knob = NULL;
static button_handle_t g_encoder_btn = NULL;
static button_handle_t g_settings_btn = NULL;

static ec11_event_t g_pending_event = EC11_EVENT_NONE;

static void knob_callback(void *arg, void *data)
{
    knob_event_t event = *(knob_event_t *)data;
    (void)arg;
    
    switch (event) {
    case KNOB_RIGHT:
        g_pending_event = EC11_EVENT_CW;
        ESP_LOGI(TAG, "CW");
        break;
    case KNOB_LEFT:
        g_pending_event = EC11_EVENT_CCW;
        ESP_LOGI(TAG, "CCW");
        break;
    default:
        break;
    }
}

static void encoder_btn_callback(void *arg, void *data)
{
    button_event_t event = *(button_event_t *)data;
    (void)arg;
    
    switch (event) {
    case BUTTON_PRESS_DOWN:
        g_pending_event = EC11_EVENT_PRESS;
        ESP_LOGI(TAG, "PRESS");
        break;
    case BUTTON_LONG_PRESS_START:
        g_pending_event = EC11_EVENT_LONG_PRESS;
        ESP_LOGI(TAG, "LONG_PRESS");
        break;
    case BUTTON_PRESS_UP:
        g_pending_event = EC11_EVENT_RELEASE;
        ESP_LOGI(TAG, "RELEASE");
        break;
    default:
        break;
    }
}

static void settings_btn_callback(void *arg, void *data)
{
    (void)arg;
    (void)data;
    g_pending_event = EC11_EVENT_SETTINGS;
    ESP_LOGI(TAG, "SETTINGS_BTN PRESS");
}

void encoder_init(void)
{
    knob_config_t knob_cfg = {
        .default_direction = 0,
        .gpio_encoder_a = EC11_A_GPIO,
        .gpio_encoder_b = EC11_B_GPIO,
        .enable_power_save = false,
    };
    g_knob = iot_knob_create(&knob_cfg);
    if (g_knob) {
        iot_knob_register_cb(g_knob, KNOB_LEFT, knob_callback, NULL);
        iot_knob_register_cb(g_knob, KNOB_RIGHT, knob_callback, NULL);
        ESP_LOGI(TAG, "Knob initialized: A=IO%d, B=IO%d", EC11_A_GPIO, EC11_B_GPIO);
    } else {
        ESP_LOGE(TAG, "Failed to create knob");
    }

    button_config_t btn_cfg = {
        .long_press_time = 1000,
        .short_press_time = 50,
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num = EC11_K_GPIO,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &g_encoder_btn);
    if (ret == ESP_OK) {
        iot_button_register_cb(g_encoder_btn, BUTTON_PRESS_DOWN, NULL, encoder_btn_callback, NULL);
        iot_button_register_cb(g_encoder_btn, BUTTON_LONG_PRESS_START, NULL, encoder_btn_callback, NULL);
        iot_button_register_cb(g_encoder_btn, BUTTON_PRESS_UP, NULL, encoder_btn_callback, NULL);
        ESP_LOGI(TAG, "Encoder button initialized: IO%d", EC11_K_GPIO);
    } else {
        ESP_LOGE(TAG, "Failed to create encoder button: %s", esp_err_to_name(ret));
    }

    button_gpio_config_t settings_gpio_cfg = {
        .gpio_num = SETTINGS_BTN_GPIO,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    ret = iot_button_new_gpio_device(&btn_cfg, &settings_gpio_cfg, &g_settings_btn);
    if (ret == ESP_OK) {
        iot_button_register_cb(g_settings_btn, BUTTON_PRESS_DOWN, NULL, settings_btn_callback, NULL);
        ESP_LOGI(TAG, "Settings button initialized: IO%d", SETTINGS_BTN_GPIO);
    } else {
        ESP_LOGE(TAG, "Failed to create settings button: %s", esp_err_to_name(ret));
    }
}

ec11_event_t encoder_get_event(void)
{
    ec11_event_t event = g_pending_event;
    g_pending_event = EC11_EVENT_NONE;
    return event;
}

bool settings_button_get_event(void)
{
    if (g_pending_event == EC11_EVENT_SETTINGS) {
        g_pending_event = EC11_EVENT_NONE;
        return true;
    }
    return false;
}

uint32_t encoder_get_press_duration_ms(void)
{
    if (g_encoder_btn) {
        return iot_button_get_pressed_time(g_encoder_btn);
    }
    return 0;
}

void encoder_deinit(void)
{
    if (g_knob) {
        iot_knob_delete(g_knob);
        g_knob = NULL;
    }
    if (g_encoder_btn) {
        iot_button_delete(g_encoder_btn);
        g_encoder_btn = NULL;
    }
    if (g_settings_btn) {
        iot_button_delete(g_settings_btn);
        g_settings_btn = NULL;
    }
}

