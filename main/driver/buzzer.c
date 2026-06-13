#include "buzzer.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "BUZZER";

#define BUZZER_GPIO GPIO_NUM_2
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER LEDC_TIMER_0
#define BUZZER_LEDC_RESOLUTION LEDC_TIMER_8_BIT
#define BUZZER_DEFAULT_FREQ 2000

static bool buzzer_initialized = false;
static uint32_t current_freq = BUZZER_DEFAULT_FREQ;
static uint8_t current_volume = 50;

/* Serializes melody playback across multiple tasks (input/service/tcp/SNTP/ui_update).
 * esp_timer_delete blocks until the running callback returns, so the callback
 * itself never races with a caller that holds this mutex. */
static SemaphoreHandle_t s_play_mutex = NULL;

void buzzer_init(void)
{
    if (buzzer_initialized) {
        return;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    ledc_timer_config_t timer_config = {
        .speed_mode = BUZZER_LEDC_MODE,
        .timer_num = BUZZER_LEDC_TIMER,
        .duty_resolution = BUZZER_LEDC_RESOLUTION,
        .freq_hz = current_freq,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    s_play_mutex = xSemaphoreCreateRecursiveMutex();
    assert(s_play_mutex);

    buzzer_initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d, freq=%dHz", BUZZER_GPIO, current_freq);
}

void buzzer_set_volume(uint8_t volume)
{
    if (!buzzer_initialized) {
        return;
    }
    current_volume = volume;
    uint32_t duty = (volume * 255) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));
}

void buzzer_set_frequency(uint32_t freq_hz)
{
    if (!buzzer_initialized) {
        return;
    }
    current_freq = freq_hz;
    ESP_ERROR_CHECK(ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz));
}

void buzzer_on(void)
{
    if (!buzzer_initialized) {
        return;
    }
    uint32_t duty = (current_volume * 255) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));
}

void buzzer_off(void)
{
    if (!buzzer_initialized) {
        return;
    }
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));
}

void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!buzzer_initialized) {
        return;
    }
    buzzer_set_frequency(freq_hz);
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_off();
}

static esp_timer_handle_t play_timer = NULL;
/* Internal copy of the melody. Caller-passed pointer may point to a stack
 * buffer or const ROM; we copy here so playback stays valid even after the
 * caller returns. 32 notes is plenty: longest hour-chime (12 hits) uses 15. */
#define BUZZER_PLAY_MAX 32
static buzzer_note_t play_buffer[BUZZER_PLAY_MAX];
static const buzzer_note_t *play_notes = NULL;
static uint8_t play_count = 0;
static uint8_t play_index = 0;
static bool playing = false;

static void play_timer_callback(void *arg)
{
    buzzer_off();
    play_index++;

    if (play_index < play_count) {
        const buzzer_note_t *note = &play_notes[play_index];
        if (note->freq_hz > 0) {
            buzzer_set_frequency(note->freq_hz);
            buzzer_on();
        }
        esp_timer_start_once(play_timer, note->duration_ms * 1000);
    } else {
        playing = false;
    }
}

void buzzer_play_melody(const buzzer_note_t *notes, uint8_t count)
{
    if (!buzzer_initialized || count == 0 || !notes) return;
    if (count > BUZZER_PLAY_MAX) count = BUZZER_PLAY_MAX;

    xSemaphoreTakeRecursive(s_play_mutex, portMAX_DELAY);

    // Stop current playback
    if (play_timer) {
        esp_timer_stop(play_timer);
        esp_timer_delete(play_timer);
        play_timer = NULL;
    }
    buzzer_off();

    /* Copy into our own buffer so caller's memory lifetime doesn't matter. */
    memcpy(play_buffer, notes, count * sizeof(buzzer_note_t));
    play_notes = play_buffer;
    play_count = count;
    play_index = 0;
    playing = true;

    const esp_timer_create_args_t timer_args = {
        .callback = &play_timer_callback,
        .name = "buzzer_play"
    };
    esp_timer_create(&timer_args, &play_timer);

    const buzzer_note_t *note = &play_notes[0];
    if (note->freq_hz > 0) {
        buzzer_set_frequency(note->freq_hz);
        buzzer_on();
    }
    esp_timer_start_once(play_timer, note->duration_ms * 1000);

    xSemaphoreGiveRecursive(s_play_mutex);
}

void buzzer_stop(void)
{
    if (!buzzer_initialized) return;

    xSemaphoreTakeRecursive(s_play_mutex, portMAX_DELAY);
    if (play_timer) {
        esp_timer_stop(play_timer);
        esp_timer_delete(play_timer);
        play_timer = NULL;
    }
    buzzer_off();
    playing = false;
    xSemaphoreGiveRecursive(s_play_mutex);
}

bool buzzer_is_playing(void)
{
    return playing;
}
