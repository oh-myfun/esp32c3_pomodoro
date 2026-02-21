#include "encoder.h"
#include "esp_log.h"
#include "esp_timer.h"
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

// 设置按键状态
static uint8_t last_settings_btn_state = 1;

// 长按检测
static uint32_t press_start_time = 0;
static bool long_press_triggered = false;
#define LONG_PRESS_DURATION_MS 1000

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

    // 初始化设置按键GPIO9
    gpio_config_t settings_btn_conf = {
        .pin_bit_mask = (1ULL << SETTINGS_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&settings_btn_conf);

    last_state = (gpio_get_level(EC11_A_GPIO) << 1) | gpio_get_level(EC11_B_GPIO);
    step_count = 0;
    last_dir = 0;

    ESP_LOGI(TAG, "Encoder initialized: A=IO%d, B=IO%d, K=IO%d",
             EC11_A_GPIO, EC11_B_GPIO, EC11_K_GPIO);
    ESP_LOGI(TAG, "Settings button initialized: IO%d", SETTINGS_BTN_GPIO);
}

ec11_event_t encoder_get_event(void)
{
    uint8_t a = gpio_get_level(EC11_A_GPIO);
    uint8_t b = gpio_get_level(EC11_B_GPIO);
    uint8_t current_state = (a << 1) | b;

    ec11_event_t event = EC11_EVENT_NONE;

    // 只在状态为00时检测（编码器稳定位置）
    if (current_state == 0 && last_state != 0) {
        if (last_state == 0b10) {
            encoder_count++;
            event = EC11_EVENT_CW;
            ESP_LOGI(TAG, "CW");
        } else if (last_state == 0b01) {
            encoder_count--;
            event = EC11_EVENT_CCW;
            ESP_LOGI(TAG, "CCW");
        }
        last_state = 0;
    } else if (current_state != last_state) {
        last_state = current_state;
    }

    // 检测按键
    uint8_t btn = gpio_get_level(EC11_K_GPIO);
    if (btn != last_button_state) {
        if (btn == 0) {
            button_pressed = true;
            press_start_time = esp_timer_get_time() / 1000;
            long_press_triggered = false;
            event = EC11_EVENT_PRESS;
            ESP_LOGI(TAG, "PRESS");
        } else {
            button_pressed = false;
            long_press_triggered = false;
            event = EC11_EVENT_RELEASE;
        }
        last_button_state = btn;
        vTaskDelay(pdMS_TO_TICKS(20));
    } else if (button_pressed && !long_press_triggered) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        if (current_time - press_start_time >= LONG_PRESS_DURATION_MS) {
            long_press_triggered = true;
            event = EC11_EVENT_LONG_PRESS;
            ESP_LOGI(TAG, "LONG_PRESS");
        }
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

// 检测设置按键是否被按下（带消抖）
bool settings_button_get_event(void)
{
    uint8_t btn = gpio_get_level(SETTINGS_BTN_GPIO);
    bool pressed = false;

    if (btn != last_settings_btn_state) {
        if (btn == 0) {
            pressed = true;
            ESP_LOGI(TAG, "SETTINGS_BTN PRESS");
        }
        last_settings_btn_state = btn;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return pressed;
}

uint32_t encoder_get_press_duration_ms(void)
{
    if (!button_pressed) {
        return 0;
    }
    uint32_t current_time = esp_timer_get_time() / 1000;
    return current_time - press_start_time;
}

