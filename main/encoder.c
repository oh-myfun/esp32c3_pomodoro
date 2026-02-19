#include "encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ENCODER";

static volatile int32_t encoder_count = 0;
static volatile bool button_pressed = false;
static uint8_t last_state = 0;
static uint8_t last_button_state = 1;

// 添加消抖和周期计数
static uint8_t step_count = 0;
static int8_t last_dir = 0;

void encoder_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EC11_A_GPIO) | (1ULL << EC11_B_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << EC11_K_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);
    
    last_state = (gpio_get_level(EC11_A_GPIO) << 1) | gpio_get_level(EC11_B_GPIO);
    step_count = 0;
    last_dir = 0;
    
    ESP_LOGI(TAG, "Encoder initialized: A=IO%d, B=IO%d, K=IO%d", 
             EC11_A_GPIO, EC11_B_GPIO, EC11_K_GPIO);
}

ec11_event_t encoder_get_event(void)
{
    uint8_t a = gpio_get_level(EC11_A_GPIO);
    uint8_t b = gpio_get_level(EC11_B_GPIO);
    uint8_t current_state = (a << 1) | b;
    
    ec11_event_t event = EC11_EVENT_NONE;
    
    // 只在状态为00时检测（编码器稳定位置）
    if (current_state == 0 && last_state != 0) {
        // 根据上一个状态判断方向
        // 顺时针序列: 00->01->11->10->00 (回到00时，上一个应该是10)
        // 逆时针序列: 00->10->11->01->00 (回到00时，上一个应该是01)
        if (last_state == 0b10) {
            encoder_count++;
            event = EC11_EVENT_CW;
        } else if (last_state == 0b01) {
            encoder_count--;
            event = EC11_EVENT_CCW;
        }
        last_state = 0;
    } else if (current_state != last_state) {
        // 记录中间状态
        last_state = current_state;
    }
    
    // 检测按键（简化消抖）
    uint8_t btn = gpio_get_level(EC11_K_GPIO);
    if (btn != last_button_state) {
        if (btn == 0) {
            button_pressed = true;
            event = EC11_EVENT_PRESS;
        } else {
            button_pressed = false;
            event = EC11_EVENT_RELEASE;
        }
        last_button_state = btn;
        vTaskDelay(pdMS_TO_TICKS(20));  // 简单消抖
    }
    
    return event;
}

int32_t encoder_get_count(void)
{
    return encoder_count;
}

void encoder_reset_count(void)
{
    encoder_count = 0;
}

bool encoder_is_pressed(void)
{
    return (gpio_get_level(EC11_K_GPIO) == 0);
}
