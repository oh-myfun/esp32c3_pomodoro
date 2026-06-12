#include "input_handler.h"
#include "driver/gpio.h"
#include "driver/st7789_lcd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "ui/ui_manager.h"
#include "service/sound_service.h"
#include "service/storage_service.h"
#include "ui/i18n.h"

#define ENCODER_A_GPIO   GPIO_NUM_4
#define ENCODER_B_GPIO  GPIO_NUM_5
#define ENCODER_K_GPIO  GPIO_NUM_21
#define SETTINGS_GPIO   GPIO_NUM_9

/* Acceleration: strict conditions, fast consecutive scrolling only */
#define ACCEL_FAST_US      100000LL  /* <100ms between ticks = fast */
#define ACCEL_RAMP         3         /* consecutive fast ticks before accel */
#define ACCEL_MAX_STEP     10

/* Long press hint delay: show progress bar after this delay */
#define HOLD_HINT_DELAY_US 500000LL  /* 500ms */

static const char *TAG = "INPUT";

static QueueHandle_t g_event_queue = NULL;

static knob_handle_t g_knob = NULL;
static button_handle_t g_encoder_btn = NULL;
static button_handle_t g_settings_btn = NULL;
static bool g_reverse_encoder = false;

static volatile int64_t s_last_enc_time = 0;
static volatile int s_enc_phys_dir = 0;    /* 1=right, -1=left, 0=none */
static volatile int s_enc_fast_count = 0;

static esp_timer_handle_t s_enc_hold_timer = NULL;
static esp_timer_handle_t s_set_hold_timer = NULL;

static int calc_encoder_step(int phys_dir)
{
    int64_t now = esp_timer_get_time();
    int64_t dt = now - s_last_enc_time;

    int step = 1;
    if (phys_dir == s_enc_phys_dir && dt < ACCEL_FAST_US) {
        s_enc_fast_count++;
        if (s_enc_fast_count > ACCEL_RAMP) {
            step = 1 + (s_enc_fast_count - ACCEL_RAMP);
            if (step > ACCEL_MAX_STEP) step = ACCEL_MAX_STEP;
        }
    } else {
        s_enc_fast_count = 0;
    }

    s_enc_phys_dir = phys_dir;
    s_last_enc_time = now;
    return step;
}

static void knob_cb(void *arg, void *data)
{
    (void)data;
    knob_handle_t knob = (knob_handle_t)arg;
    knob_event_t event = iot_knob_get_event(knob);
    int count = iot_knob_get_count_value(knob);
    if ((count & 1) == 0) {
        return;
    }

    input_event_t e = { .type = INPUT_EVENT_NONE, .step = 1 };
    int phys_dir = 0;
    switch (event) {
    case KNOB_RIGHT: phys_dir = 1;  e.type = INPUT_EVENT_ENCODER_CW;  break;
    case KNOB_LEFT:  phys_dir = -1; e.type = INPUT_EVENT_ENCODER_CCW; break;
    default: return;
    }

    e.step = (uint8_t)calc_encoder_step(phys_dir);

    if (g_event_queue) {
        xQueueSendFromISR(g_event_queue, &e, NULL);
    }
}

/* Track button held state for simultaneous long press detection */
static volatile bool s_encoder_held = false;
static volatile bool s_settings_held = false;

static void send_event(input_event_t *e)
{
    if (g_event_queue) {
        xQueueSendFromISR(g_event_queue, e, NULL);
    }
}

static void encoder_hold_timer_cb(void *arg)
{
    (void)arg;
    input_event_t e = { .type = INPUT_EVENT_ENCODER_HOLD, .step = 1 };
    if (g_event_queue) {
        xQueueSend(g_event_queue, &e, 0);
    }
}

static void settings_hold_timer_cb(void *arg)
{
    (void)arg;
    input_event_t e = { .type = INPUT_EVENT_SETTINGS_HOLD, .step = 1 };
    if (g_event_queue) {
        xQueueSend(g_event_queue, &e, 0);
    }
}

static void encoder_btn_cb(void *arg, void *data)
{
    (void)data;
    button_handle_t btn = (button_handle_t)arg;
    button_event_t event = iot_button_get_event(btn);
    input_event_t e = { .type = INPUT_EVENT_NONE, .step = 1 };

    switch (event) {
    case BUTTON_PRESS_DOWN:
        s_encoder_held = true;
        if (s_enc_hold_timer) {
            esp_timer_stop(s_enc_hold_timer);
            esp_timer_start_once(s_enc_hold_timer, HOLD_HINT_DELAY_US);
        }
        break;
    case BUTTON_PRESS_UP:
        s_encoder_held = false;
        if (s_enc_hold_timer) esp_timer_stop(s_enc_hold_timer);
        break;
    case BUTTON_SINGLE_CLICK:
        e.type = INPUT_EVENT_ENCODER_PRESS;
        break;
    case BUTTON_LONG_PRESS_UP:
        s_encoder_held = false;
        if (s_enc_hold_timer) esp_timer_stop(s_enc_hold_timer);
        if (s_settings_held) {
            e.type = INPUT_EVENT_DUAL_LONG_PRESS;
        } else {
            e.type = INPUT_EVENT_ENCODER_LONG_PRESS;
        }
        break;
    default:
        break;
    }
    if (e.type != INPUT_EVENT_NONE) send_event(&e);
}

static void settings_btn_cb(void *arg, void *data)
{
    (void)arg;
    (void)data;
    button_handle_t btn = (button_handle_t)arg;
    button_event_t event = iot_button_get_event(btn);
    input_event_t e = { .type = INPUT_EVENT_NONE, .step = 1 };

    switch (event) {
    case BUTTON_PRESS_DOWN:
        s_settings_held = true;
        if (s_set_hold_timer) {
            esp_timer_stop(s_set_hold_timer);
            esp_timer_start_once(s_set_hold_timer, HOLD_HINT_DELAY_US);
        }
        break;
    case BUTTON_PRESS_UP:
        s_settings_held = false;
        if (s_set_hold_timer) esp_timer_stop(s_set_hold_timer);
        break;
    case BUTTON_SINGLE_CLICK:
        e.type = INPUT_EVENT_SETTINGS_PRESS;
        break;
    case BUTTON_LONG_PRESS_UP:
        s_settings_held = false;
        if (s_set_hold_timer) esp_timer_stop(s_set_hold_timer);
        if (s_encoder_held) {
            e.type = INPUT_EVENT_DUAL_LONG_PRESS;
        } else {
            e.type = INPUT_EVENT_SETTINGS_LONG_PRESS;
        }
        break;
    default:
        break;
    }
    if (e.type != INPUT_EVENT_NONE) send_event(&e);
}

void input_handler_init(void)
{
    g_event_queue = xQueueCreate(8, sizeof(input_event_t));
    assert(g_event_queue);

    esp_timer_create_args_t enc_timer_args = {
        .callback = encoder_hold_timer_cb,
        .name = "enc_hold",
    };
    esp_timer_create_args_t set_timer_args = {
        .callback = settings_hold_timer_cb,
        .name = "set_hold",
    };
    esp_timer_create(&enc_timer_args, &s_enc_hold_timer);
    esp_timer_create(&set_timer_args, &s_set_hold_timer);

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
        iot_button_register_cb(g_encoder_btn, BUTTON_PRESS_DOWN, NULL, encoder_btn_cb, NULL);
        iot_button_register_cb(g_encoder_btn, BUTTON_PRESS_UP, NULL, encoder_btn_cb, NULL);
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
        iot_button_register_cb(g_settings_btn, BUTTON_SINGLE_CLICK, NULL, settings_btn_cb, g_settings_btn);
        iot_button_register_cb(g_settings_btn, BUTTON_LONG_PRESS_UP, NULL, settings_btn_cb, g_settings_btn);
        iot_button_register_cb(g_settings_btn, BUTTON_PRESS_DOWN, NULL, settings_btn_cb, g_settings_btn);
        iot_button_register_cb(g_settings_btn, BUTTON_PRESS_UP, NULL, settings_btn_cb, g_settings_btn);
    }

    int32_t enc_dir = 0;
    storage_load_int(STORAGE_NAMESPACE_SETTINGS, KEY_ENC_DIR, &enc_dir);
    g_reverse_encoder = (enc_dir != 0);
    ESP_LOGI(TAG, "Encoder direction: %s", g_reverse_encoder ? "reversed" : "normal");
}

void input_handler_task(void *arg)
{
    input_event_t event;

    while (1) {
        if (xQueueReceive(g_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            extern bool activity_touch(void);
            if (activity_touch()) continue;

            lvgl_lock();

            if (event.type != INPUT_EVENT_ENCODER_HOLD &&
                event.type != INPUT_EVENT_SETTINGS_HOLD) {
                ui_hide_long_press_hint();
            }

            switch (event.type) {
                case INPUT_EVENT_ENCODER_HOLD: {
                    const char *action = s_settings_held
                        ? i18n(STR_ACT_RESET)
                        : ui_get_long_press_action(false);
                    if (action) ui_show_long_press_hint(action);
                    break;
                }
                case INPUT_EVENT_SETTINGS_HOLD: {
                    const char *action = s_encoder_held
                        ? i18n(STR_ACT_RESET)
                        : ui_get_long_press_action(true);
                    if (action) ui_show_long_press_hint(action);
                    break;
                }
                case INPUT_EVENT_ENCODER_CW:
                    ui_set_encoder_step(event.step);
                    if (g_reverse_encoder) ui_dispatch_encoder_ccw();
                    else ui_dispatch_encoder_cw();
                    break;
                case INPUT_EVENT_ENCODER_CCW:
                    ui_set_encoder_step(event.step);
                    if (g_reverse_encoder) ui_dispatch_encoder_cw();
                    else ui_dispatch_encoder_ccw();
                    break;
                case INPUT_EVENT_ENCODER_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_encoder_press();
                    break;
                case INPUT_EVENT_ENCODER_LONG_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_encoder_long_press();
                    break;
                case INPUT_EVENT_SETTINGS_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_settings_press();
                    break;
                case INPUT_EVENT_SETTINGS_LONG_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ui_dispatch_settings_long_press();
                    break;
                case INPUT_EVENT_DUAL_LONG_PRESS:
                    sound_service_play(SOUND_KEY_CLICK);
                    ESP_LOGI(TAG, "Dual long press: screen reset");
                    st7789_lcd_reinit();
                    break;
                default:
                    break;
            }
            lvgl_unlock();
        }
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
