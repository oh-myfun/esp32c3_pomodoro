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
    
    if (current_state != last_state) {
        uint8_t sum = (last_state << 2) | current_state;
        int8_t dir = 0;
        
        // 检测方向
        if (sum == 0b0001 || sum == 0b0111 || sum == 0b1110 || sum == 0b1000) {
            dir = 1;  // CW
        } else if (sum == 0b0010 || sum == 0b1011 || sum == 0b1101 || sum == 0b0100) {
            dir = -1; // CCW
        }
        
        last_state = current_state;
        
        // 方向改变或重新开始计数
        if (dir != 0) {
            if (dir != last_dir) {
                step_count = 1;
                last_dir = dir;
            } else {
                step_count++;
            }
            
            // 每4步（一个完整周期）触发一次事件
            if (step_count >= 4) {
                step_count = 0;
                if (dir > 0) {
                    encoder_count++;
                    event = EC11_EVENT_CW;
                } else {
                    encoder_count--;
                    event = EC11_EVENT_CCW;
                }
            }
        }
    }
    
    // 检测按键
    uint8_t btn = gpio_get_level(EC11_K_GPIO);
    static uint32_t last_btn_time = 0;
    uint32_t now = xTaskGetTickCount();
    
    if (btn != last_button_state && (now - last_btn_time) > pdMS_TO_TICKS(50)) {
        if (btn == 0) {
            button_pressed = true;
            event = EC11_EVENT_PRESS;
            ESP_LOGI(TAG, "Button pressed");
        } else {
            button_pressed = false;
            event = EC11_EVENT_RELEASE;
        }
        last_button_state = btn;
        last_btn_time = now;
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
