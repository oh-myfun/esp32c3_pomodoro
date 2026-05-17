#include "backlight.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "BACKLIGHT";

#define BL_GPIO             GPIO_NUM_20
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_RESOLUTION  LEDC_TIMER_10_BIT
#define BL_PWM_FREQ         5000
#define BL_FULL_DUTY        ((1 << BL_LEDC_RESOLUTION) - 1)

#define BL_LEVEL_MIN  1
#define BL_LEVEL_MAX  10

static bool initialized = false;
static uint8_t brightness_level = BL_LEVEL_MAX;

void backlight_init(void)
{
    if (initialized) return;

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RESOLUTION,
        .freq_hz         = BL_PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* Low-level drives transistor ON → inverted duty */
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = BL_GPIO,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    initialized = true;
    ESP_LOGI(TAG, "Backlight initialized on GPIO%d", BL_GPIO);
}

/* Exponential brightness mapping (perceptually uniform).
   Level 1=5% .. Level 10=100%, gamma~2.2 curve */
static const uint8_t bl_level_map[BL_LEVEL_MAX] = {
    5, 10, 18, 28, 40, 52, 65, 78, 90, 100
};

void backlight_set_brightness(uint8_t level)
{
    if (!initialized) return;
    if (level < BL_LEVEL_MIN) level = BL_LEVEL_MIN;
    if (level > BL_LEVEL_MAX) level = BL_LEVEL_MAX;
    brightness_level = level;

    uint8_t percent = bl_level_map[level - 1];
    uint32_t duty = BL_FULL_DUTY - (percent * BL_FULL_DUTY) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));
}

uint8_t backlight_get_brightness(void)
{
    return brightness_level;
}
