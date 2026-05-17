#include "input_handler.h"
#include "driver/gpio.h"
#include "driver/st7789_lcd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "ui/ui_manager.h"
#include "service/sound_service.h"
#include "service/storage_service.h"

#define ENCODER_A_GPIO   GPIO_NUM_4
#define ENCODER_B_GPIO  GPIO_NUM_5
#define ENCODER_K_GPIO  GPIO_NUM_21
#define SETTINGS_GPIO   GPIO_NUM_9

static const char *TAG = "INPUT";

static QueueHandle_t g_event_queue = NULL;

static knob_handle_t g_knob = NULL;
static button_handle_t g_encoder_btn = NULL;
static button_handle_t g_settings_btn = NULL;
static bool g_reverse_encoder = false;

static void knob_cb(void *arg, void *data)
{
    (void)data;
    knob_handle_t knob = (knob_handle_t)arg;
    knob_event_t event = iot_knob_get_event(knob);
    int count = iot_knob_get_count_value(knob);
    if ((count & 1) == 0) {
        return;
    }
    input_event_t e = INPUT_EVENT_NONE;
    switch (event) {
    case KNOB_RIGHT:
        e = INPUT_EVENT_ENCODER_CW;
        break;
    case KNOB_LEFT:
        e = INPUT_EVENT_ENCODER_CCW;
        break;
    default:
        break;
    }
    if (e != INPUT_EVENT_NONE && g_event_queue) {
        xQueueSendFromISR(g_event_queue, &e, NULL);
    }
}

static void encoder_btn_cb(void *arg, void *data)
{
    (void)data;
    button_handle_t btn = (button_handle_t)arg;
    button_event_t event = iot_button_get_event(btn);
    input_event_t e = INPUT_EVENT_NONE;
    switch (event) {
    case BUTTON_SINGLE_CLICK:
        e = INPUT_EVENT_ENCODER_PRESS;
        break;
    case BUTTON_LONG_PRESS_UP:
        e = INPUT_EVENT_ENCODER_LONG_PRESS;
        break;
    default:
        break;
    }
    if (e != INPUT_EVENT_NONE && g_event_queue) {
        xQueueSendFromISR(g_event_queue, &e, NULL);
    }
}

static void settings_btn_cb(void *arg, void *data)
{
    (void)arg;
    (void)data;
    input_event_t e = INPUT_EVENT_SETTINGS_PRESS;
    if (g_event_queue) {
        xQueueSendFromISR(g_event_queue, &e, NULL);
    }
}

void input_handler_init(void)
{
    g_event_queue = xQueueCreate(8, sizeof(input_event_t));
    assert(g_event_queue);

    knob_config_t knob_cfg = {
        .default_direction = 0,
        .gpio_encoder_a = ENCODER_A_GPIO,
        .gpio_encoder_b = ENCODER_B_GPIO,
        .enable_power_save = false,
    };
    g_knob = iot_knob_create(&knob_cfg);
    if (!g_knob) {
        ESP_LOGE(TAG, "Failed to create knob");
    } else {
        iot_knob_register_cb(g_knob, KNOB_LEFT, knob_cb, NULL);
        iot_knob_register_cb(g_knob, KNOB_RIGHT, knob_cb, NULL);
    }

    button_config_t btn_cfg = {
        .long_press_time = 1000,
        .short_press_time = 50,
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num = ENCODER_K_GPIO,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &g_encoder_btn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create encoder button: %s", esp_err_to_name(ret));
    } else {
        iot_button_register_cb(g_encoder_btn, BUTTON_SINGLE_CLICK, NULL, encoder_btn_cb, NULL);
        iot_button_register_cb(g_encoder_btn, BUTTON_LONG_PRESS_UP, NULL, encoder_btn_cb, NULL);
    }

    button_gpio_config_t settings_gpio_cfg = {
        .gpio_num = SETTINGS_GPIO,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    ret = iot_button_new_gpio_device(&btn_cfg, &settings_gpio_cfg, &g_settings_btn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create settings button: %s", esp_err_to_name(ret));
    } else {
        iot_button_register_cb(g_settings_btn, BUTTON_SINGLE_CLICK, NULL, settings_btn_cb, NULL);
    }

    // Load encoder direction from NVS
    int32_t enc_dir = 0;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &enc_dir);
    g_reverse_encoder = (enc_dir != 0);
    ESP_LOGI(TAG, "Encoder direction: %s", g_reverse_encoder ? "reversed" : "normal");
}

void input_handler_task(void *arg)
{
    input_event_t event;

    while (1) {
        while (xQueueReceive(g_event_queue, &event, 0) == pdTRUE) {
            lvgl_lock();
            switch (event) {
                case INPUT_EVENT_ENCODER_CW:
                    if (g_reverse_encoder) ui_dispatch_encoder_ccw();
                    else ui_dispatch_encoder_cw();
                    break;
                case INPUT_EVENT_ENCODER_CCW:
                    if (g_reverse_encoder) ui_dispatch_encoder_cw();
                    else ui_dispatch_encoder_ccw();
                    break;
                case INPUT_EVENT_ENCODER_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_encoder_press();
                    break;
                case INPUT_EVENT_ENCODER_LONG_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ESP_LOGI(TAG, "Global: screen reset");
                    st7789_lcd_reinit();
                    break;
                case INPUT_EVENT_SETTINGS_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_settings_press();
                    break;
                default:
                    break;
            }
            lvgl_unlock();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void input_handler_set_reverse(bool reverse)
{
    g_reverse_encoder = reverse;
    ESP_LOGI(TAG, "Encoder reverse: %s", reverse ? "on" : "off");
}

bool input_handler_get_reverse(void)
{
    return g_reverse_encoder;
}
