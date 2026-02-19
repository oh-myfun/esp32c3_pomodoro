#include "encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ENCODER";

static volatile int32_t encoder_count = 0;
static volatile bool button_pressed = false;
static uint8_t last_state = 0;
static uint8_t last_button_state = 1;  // 上拉输入，默认高电平

void encoder_init(void)
{
    // 配置A、B相为输入（上拉）
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EC11_A_GPIO) | (1ULL << EC11_B_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // 配置K键为输入（上拉）
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << EC11_K_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);
    
    // 初始化状态
    last_state = (gpio_get_level(EC11_A_GPIO) << 1) | gpio_get_level(EC11_B_GPIO);
    
    ESP_LOGI(TAG, "Encoder initialized: A=IO%d, B=IO%d, K=IO%d", 
             EC11_A_GPIO, EC11_B_GPIO, EC11_K_GPIO);
}

ec11_event_t encoder_get_event(void)
{
    // 读取当前状态
    uint8_t a = gpio_get_level(EC11_A_GPIO);
    uint8_t b = gpio_get_level(EC11_B_GPIO);
    uint8_t current_state = (a << 1) | b;
    
    ec11_event_t event = EC11_EVENT_NONE;
    
    // 状态变化检测旋转
    if (current_state != last_state) {
        // 根据状态变化判断方向
        // 顺时针: 00->01->11->10->00
        // 逆时针: 00->10->11->01->00
        uint8_t sum = (last_state << 2) | current_state;
        
        // 有效状态转换
        if (sum == 0b0001 || sum == 0b0111 || sum == 0b1110 || sum == 0b1000) {
            encoder_count++;
            event = EC11_EVENT_CW;
        } else if (sum == 0b0010 || sum == 0b1011 || sum == 0b1101 || sum == 0b0100) {
            encoder_count--;
            event = EC11_EVENT_CCW;
        }
        
        last_state = current_state;
    }
    
    // 检测按键
    uint8_t btn = gpio_get_level(EC11_K_GPIO);
    if (btn != last_button_state) {
        if (btn == 0) {
            // 按键按下（低电平）
            button_pressed = true;
            event = EC11_EVENT_PRESS;
            ESP_LOGI(TAG, "Button pressed");
        } else {
            // 按键释放
            button_pressed = false;
            event = EC11_EVENT_RELEASE;
        }
        last_button_state = btn;
        vTaskDelay(20 / portTICK_PERIOD_MS);  // 消抖延时
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
