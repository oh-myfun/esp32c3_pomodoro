#include "input_handler.h"
#include "driver/encoder.h"
#include "driver/buzzer.h"
#include "ui/ui_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "INPUT_HANDLER";

void input_handler_init(void)
{
    encoder_init();
    ESP_LOGI(TAG, "Input handler initialized");
}

void input_handler_task(void *arg)
{
    ESP_LOGI(TAG, "Input handler task started");

    while (1) {
        ec11_event_t event = encoder_get_event();

        if (event != EC11_EVENT_NONE) {
            switch (event) {
                case EC11_EVENT_CW:
                    ui_dispatch_encoder_cw();
                    break;
                case EC11_EVENT_CCW:
                    ui_dispatch_encoder_ccw();
                    break;
                case EC11_EVENT_PRESS:
                    ui_dispatch_encoder_press();
                    break;
                case EC11_EVENT_LONG_PRESS:
                    ui_dispatch_encoder_long_press();
                    break;
                case EC11_EVENT_SETTINGS:
                    ui_dispatch_settings_press();
                    break;
                default:
                    break;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
